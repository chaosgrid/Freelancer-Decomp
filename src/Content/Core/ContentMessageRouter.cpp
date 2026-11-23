// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Content.dll TU36 (06f056d0-06f05d00): the StateMachine message router. See
// ContentMessageRouter.h.

#include "Core/ContentMessageRouter.h"
#include "Core/ContentInternal.h"
#include "Core/ContentStaticIds.h" // TU36 static IDs 06f056d0..06f058b0 (DAT_06fd0638..DAT_06fd05fc)

FContentMessageRouter* GMessageRouter = nullptr;

FContentMessageRouterBase::FContentMessageRouterBase()
{
    GMessageRouter = static_cast<FContentMessageRouter*>(this);
}

// FUN_06f05cf0.
FContentMessageRouterBase::~FContentMessageRouterBase()
{
    GMessageRouter = nullptr;
}

// FUN_06f058d0 (06f058d0-06f05968).
FContentMessageRouter::FContentMessageRouter()
    : TimerId(GContentNextUniqueId++)
    , LastHandlerId(0)
{
}

// FUN_06f05990 (06f05990-06f05a6f). The handlers themselves are not deleted.
FContentMessageRouter::~FContentMessageRouter()
{
    std::list<StateMachineMessage*>::iterator It = PendingMessages.begin();
    for (; It != PendingMessages.end(); ++It)
    {
        StateMachineMessage* Pending = *It;
        if (Pending != nullptr)
            delete Pending;
    }
}

// LAB_06f05a70 (06f05a70-06f05b85). Delay <= 0.0f (DAT_06fb444c): deliver now. Otherwise
// (greater or unordered) queue a clone and arm a timer.
void FContentMessageRouter::RouteMessage(StateMachineMessage* Msg, float Delay,
                                         unsigned int Target, unsigned int Sender, int Scope)
{
    if (Delay <= 0.0f)
    {
        Msg->set_delivery_options(0.0f, Sender, Target, Scope);
        MessageHandler* Handler = LookupHandlerById(Msg->Receiver);
        if (Handler == nullptr)
            return;
        Msg->bDelivered = 1;
        Handler->receive_message(Msg);
        return;
    }

    float DeliveryTime = static_cast<float>(GetContentGameTime() + Delay);

    // Insert before the first queued message whose time is greater.
    std::list<StateMachineMessage*>::iterator Where = PendingMessages.begin();
    for (; Where != PendingMessages.end(); ++Where)
    {
        if ((*Where)->DeliveryTime > DeliveryTime)
            break;
    }

    StateMachineMessage* Copy = Msg->clone();
    Copy->set_delivery_options(DeliveryTime, Sender, Target, Scope);
    PendingMessages.insert(Where, Copy);

    pub::Controller::TimerExpired Timer;
    Timer.Param1 = TimerId;
    Timer.Param2 = 0;
    pub::SetTimer(GSpaceSupportControllerId, Timer, Delay);
}

// FUN_06f05c00 (06f05c00-06f05c42).
unsigned int FContentMessageRouter::RegisterHandler(MessageHandler* Handler)
{
    Handlers.push_back(Handler);
    ++LastHandlerId;
    return LastHandlerId;
}

// FUN_06f05c50 (06f05c50-06f05c8f).
void FContentMessageRouter::UnregisterHandler(MessageHandler* Handler)
{
    std::list<MessageHandler*>::iterator It = Handlers.begin();
    for (; It != Handlers.end(); ++It)
    {
        if (*It == Handler)
        {
            Handlers.erase(It);
            return;
        }
    }
}

// FUN_06f05ca0 (06f05ca0-06f05ce0).
MessageHandler* FContentMessageRouter::LookupHandlerById(unsigned int Id)
{
    std::list<MessageHandler*>::iterator It = Handlers.begin();
    for (; It != Handlers.end(); ++It)
    {
        if ((*It)->get_id() == Id)
            return *It;
    }
    return nullptr;
}

// LAB_06f05b90 (06f05b90-06f05bf2). Only the timer armed by this router
// is handled: the earliest queued message is delivered like an immediate one, then
// deleted. Retail takes the first node without an emptiness test: every queued message armed
// exactly one timer.
void FContentMessageRouter::OnTimerExpired(pub::Controller::TimerExpired const* Timer)
{
    if (Timer->Param1 != TimerId)
        return;

    std::list<StateMachineMessage*>::iterator First = PendingMessages.begin();
    StateMachineMessage* Msg = *First;
    MessageHandler* Handler = LookupHandlerById(Msg->Receiver);
    if (Handler != nullptr)
    {
        Msg->bDelivered = 1;
        Handler->receive_message(Msg);
    }
    if (Msg != nullptr)
        delete Msg;
    PendingMessages.erase(First);
}
