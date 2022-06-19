#include "svc/ControlProcess.h"
#include "memory.h"
#include "mmu.h"
#include "synchronization.h"

typedef bool (*ThreadPredicate)(KThread *thread);

// Lock bit has to be different from Rosalina to avoid unintended unlock when using Rosalina menu
static void rescheduleThread(KThread *thread, bool lock)
{
    KRecursiveLock__Lock(criticalSectionLock);

    u32 oldSchedulingMask = thread->schedulingMask;
    if(lock)
        thread->schedulingMask |= 0x20;
    else
        thread->schedulingMask &= ~0x20;

    KScheduler__AdjustThread(currentCoreContext->objectContext.currentScheduler, thread, oldSchedulingMask);

    KRecursiveLock__Unlock(criticalSectionLock);
}

static void lockThread(KThread *thread)
{
    KThread *syncThread = synchronizationMutex->owner;

    if(syncThread == NULL || syncThread != thread)
        rescheduleThread(thread, true);
}

Result  ControlProcess(Handle processHandle, ProcessOp op, u32 varg2, u32 varg3)
{
    Result              res = 0;
    KProcess            *process;
    KProcessHandleTable *handleTable = handleTableOfProcess(currentCoreContext->objectContext.currentProcess);

    if(processHandle == CUR_PROCESS_HANDLE)
    {
        process = currentCoreContext->objectContext.currentProcess;
        KAutoObject__AddReference((KAutoObject *)process);
    }
    else
        process = KProcessHandleTable__ToKProcess(handleTable, processHandle);

    if(process == NULL)
        return 0xD8E007F7; // invalid handle

    switch (op)
    {
        case PROCESSOP_GET_ALL_HANDLES:
        {
            KProcessHandleTable *table = handleTableOfProcess(process);
            u32                 *originalHandleList = (u32 *)varg2;
            u32                 count = 0;
            u32                 searchForToken = varg3;
            HandleDescriptor    *handleDesc = table->handleTable == NULL ? table->internalTable : table->handleTable;

            for (u32 idx = 0; idx < (u32)table->maxHandleCount; ++idx, ++handleDesc)
            {
                if (handleDesc->pointer == NULL)
                    continue;

                if (searchForToken)
                {
                    KClassToken token;

                    handleDesc->pointer->vtable->GetClassToken(&token, handleDesc->pointer);
                    if (searchForToken != token.flags)
                        continue;
                }

                *originalHandleList++ = idx | ((handleDesc->info << 16) >> 1);
                ++count;
            }
            res = count;
            break;
        }

        case PROCESSOP_SET_MMU_TO_RWX:
        {
            KProcessHwInfo  *hwInfo = hwInfoOfProcess(process);

            *KPROCESS_GET_PTR(process, customFlags) |= ForceRWXPages;
            KProcessHwInfo__SetMMUTableToRWX(hwInfo);
            break;
        }
        case PROCESSOP_GET_ON_MEMORY_CHANGE_EVENT:
        {
            // Only accept current process for this command
            if (process != currentCoreContext->objectContext.currentProcess)
            {
                res = 0xD8E007F7; // invalid handle
                break;
            }

            Handle  *onMemoryLayoutChangeEvent = KPROCESS_GET_PTR(process, onMemoryLayoutChangeEvent);

            if (*onMemoryLayoutChangeEvent == 0)
                res = CreateEvent(onMemoryLayoutChangeEvent, RESET_ONESHOT);

            if (res >= 0)
            {
                *KPROCESS_GET_PTR(process, customFlags) |= SignalOnMemLayoutChanges;
                KAutoObject * event = KProcessHandleTable__ToKAutoObject(handleTable, *onMemoryLayoutChangeEvent);

                createHandleForThisProcess((Handle *)varg2, event);
                ((KAutoObject *)event)->vtable->DecrementReferenceCount((KAutoObject *)event);
            }

            break;
        }

        case PROCESSOP_SIGNAL_ON_EXIT:
        {
            *KPROCESS_GET_PTR(process, customFlags) |= SignalOnExit;
            break;
        }
        case PROCESSOP_GET_PA_FROM_VA:
        {
            KProcessHwInfo  *hwInfo = hwInfoOfProcess(process);

            u32 pa = KProcessHwInfo__GetPAFromVA(hwInfo, varg3);
            *(u32 *)varg2 = pa;

            if (pa == 0)
                res = 0xE0E01BF5; ///< Invalid address

            break;
        }
        case PROCESSOP_SCHEDULE_THREADS:
        {
            ThreadPredicate threadPredicate = (ThreadPredicate)varg3;

            KRecursiveLock__Lock(criticalSectionLock);

            if (varg2 == 0) // Unlock
            {
                for (KLinkedListNode *node = threadList->list.nodes.first; node != (KLinkedListNode *)&threadList->list.nodes; node = node->next)
                {
                    KThread *thread = (KThread *)node->key;

                    if ((thread->schedulingMask & 0xF) == 2) // thread is terminating
                        continue;

                    if (thread->ownerProcess == process && (thread->schedulingMask & 0x20)
                       && (threadPredicate == NULL || threadPredicate(thread)))
                        rescheduleThread(thread, false);
                }
            }
            else // Lock
            {
                bool currentThreadsFound = false;

                for(KLinkedListNode *node = threadList->list.nodes.first; node != (KLinkedListNode *)&threadList->list.nodes; node = node->next)
                {
                    KThread *thread = (KThread *)node->key;

                    if(thread->ownerProcess != process
                       || (threadPredicate != NULL && !threadPredicate(thread)))
                        continue;

                    if(thread == coreCtxs[thread->coreId].objectContext.currentThread)
                        currentThreadsFound = true;
                    else
                        lockThread(thread);
                }

                if(currentThreadsFound)
                {
                    for(KLinkedListNode *node = threadList->list.nodes.first; node != (KLinkedListNode *)&threadList->list.nodes; node = node->next)
                    {
                        KThread *thread = (KThread *)node->key;

                        if(thread->ownerProcess != process
                           || (threadPredicate != NULL && !threadPredicate(thread)))
                            continue;

                        if(!(thread->schedulingMask & 0x20))
                        {
                            lockThread(thread);
                            KRecursiveLock__Lock(criticalSectionLock);
                            if(thread->coreId != getCurrentCoreID())
                            {
                                u32 cpsr = __get_cpsr();
                                __disable_irq();
                                coreCtxs[thread->coreId].objectContext.currentScheduler->triggerCrossCoreInterrupt = true;
                                currentCoreContext->objectContext.currentScheduler->triggerCrossCoreInterrupt = true;
                                __set_cpsr_cx(cpsr);
                            }
                            KRecursiveLock__Unlock(criticalSectionLock);
                        }
                    }
                    KScheduler__TriggerCrossCoreInterrupt(currentCoreContext->objectContext.currentScheduler);
                }
            }

            KRecursiveLock__Unlock(criticalSectionLock);
            break;
        }
        default:
            res = 0xF8C007F4;
    }

    ((KAutoObject *)process)->vtable->DecrementReferenceCount((KAutoObject *)process);

    return res;
}
