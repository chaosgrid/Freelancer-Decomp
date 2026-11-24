// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Common.dll state machines: SMEventData and its events, StateMachineMessage / SMMChangeState,
// StateMachine, MessageHandler, StateMachineMessageHandler and StateMachineGroup.

#include "common_net.h"
#include "common_misc.h" // SMM_CHANGE_STATE (SMMChangeState's message-type id)

#include <cstdlib>
#include <cstring>
#include <new>

// ===========================================================================
// SMEventData
// ===========================================================================

SMEventData::SMEventData(STATE_MACHINE_EVENT_TYPE Type)
    : EventType(Type)
{
}

STATE_MACHINE_EVENT_TYPE SMEventData::get_event_type() const
{
    // 0x062d1ad0 (ICF-shared).
    return EventType;
}

bool SMEventData::is_sm_event() const
{
    // 0x06333060, through the virtual get_event_type.
    return get_event_type() < 4;
}

bool SMEventData::is_user_event() const
{
    // 0x06333070
    return get_event_type() >= 4;
}

// ===========================================================================
// SMControllerEvent
// ===========================================================================

SMControllerEvent::SMControllerEvent(int Id, const void* Data)
    : SMEventData(SMET_Controller)
    , ControllerId(Id)
    , DataPtr(Data)
{
}

// ===========================================================================
// SMMessage
// ===========================================================================

SMMessage::SMMessage(StateMachineMessage* Msg)
    : SMEventData(SMET_Message)
    , Message(Msg)
{
}

// ===========================================================================
// StateMachineMessage
// ===========================================================================

unsigned int StateMachineMessage::s_unique_message_type_generator = 0;

StateMachineMessage::StateMachineMessage()
    : MessageType(0)
    , Sender(0)
    , Receiver(0)
    , DeliveryState(-1)
    , DeliveryTime(0.0f)
    , bDelivered(0)
{
}

StateMachineMessage::StateMachineMessage(unsigned int Type)
    : MessageType(Type)
    , Sender(0)
    , Receiver(0)
    , DeliveryState(-1)
    , DeliveryTime(0.0f)
    , bDelivered(0)
{
}

StateMachineMessage::StateMachineMessage(float Time, unsigned int Type, unsigned int InSender,
                                         unsigned int InReceiver, int State)
    : MessageType(Type)
    , Sender(InSender)
    , Receiver(InReceiver)
    , DeliveryState(State)
    , DeliveryTime(Time)
    , bDelivered(0)
{
    // RVA 0xd32f0. Retail hardcodes bDelivered = 0 (there is no Delivered parameter).
}

StateMachineMessage::StateMachineMessage(const StateMachineMessage& Other)
    : MessageType(Other.MessageType)
    , Sender(Other.Sender)
    , Receiver(Other.Receiver)
    , DeliveryState(Other.DeliveryState)
    , DeliveryTime(Other.DeliveryTime)
    , bDelivered(Other.bDelivered)
{
}

StateMachineMessage::~StateMachineMessage()
{
}

StateMachineMessage* StateMachineMessage::clone() const
{
    // RVA 0xd2ef0 (class operator new, i.e. malloc(0x1c), and the copy ctor inlined).
    return new StateMachineMessage(*this);
}

void StateMachineMessage::set_delivery_options(float Time, unsigned int InSender,
                                              unsigned int InReceiver, int State)
{
    DeliveryTime  = Time;
    Sender        = InSender;
    Receiver      = InReceiver;
    DeliveryState = State;
    bDelivered    = 0;
}

void* StateMachineMessage::operator new(size_t Size) noexcept
{
    // 0x062fd560: msvcrt malloc
    return malloc(Size);
}

void StateMachineMessage::operator delete(void* Ptr)
{
    // 0x062fd570: msvcrt free
    free(Ptr);
}

// The accessors below are ICF-shared one-line bodies.
unsigned int StateMachineMessage::get_message_type() const
{
    // 0x062d1ad0
    return MessageType;
}

void StateMachineMessage::set_message_type(unsigned int Type)
{
    // 0x06309b30
    MessageType = Type;
}

unsigned int StateMachineMessage::get_sender() const
{
    // 0x062a66a0
    return Sender;
}

void StateMachineMessage::set_sender(unsigned int InSender)
{
    // 0x0631d940
    Sender = InSender;
}

unsigned int StateMachineMessage::get_receiver() const
{
    // 0x0629fb90
    return Receiver;
}

void StateMachineMessage::set_receiver(unsigned int InReceiver)
{
    // 0x06332f40
    Receiver = InReceiver;
}

int StateMachineMessage::get_delivery_state() const
{
    // 0x06263a70
    return DeliveryState;
}

void StateMachineMessage::set_delivery_state(int State)
{
    // 0x06332f50
    DeliveryState = State;
}

float StateMachineMessage::get_delivery_time() const
{
    // 0x06332f60
    return DeliveryTime;
}

void StateMachineMessage::set_delivery_time(float Time)
{
    // 0x0633bad0 (raw dword copy)
    DeliveryTime = Time;
}

bool StateMachineMessage::is_delivered() const
{
    // 0x06263bc0
    return bDelivered != 0;
}

void StateMachineMessage::set_delivered(bool bInDelivered)
{
    // 0x06263bd0
    bDelivered = bInDelivered ? 1 : 0;
}

unsigned int StateMachineMessage::GenerateUniqueMessageType()
{
    s_unique_message_type_generator++;
    return s_unique_message_type_generator;
}

// ===========================================================================
// SMMChangeState
// ===========================================================================

SMMChangeState::SMMChangeState(int NewStateVal)
    : StateMachineMessage()
    , NewState(NewStateVal)
{
    // Retail loads the global SMM_CHANGE_STATE (a runtime-assigned id, see common_misc.cpp),
    // not a literal.
    MessageType = SMM_CHANGE_STATE;
}

StateMachineMessage* SMMChangeState::clone() const
{
    // RVA 0xd2fd0 (malloc(0x20) and the copy ctor inlined).
    return new SMMChangeState(*this);
}

// ===========================================================================
// StateMachine
// ===========================================================================

StateMachine::StateMachine()
    : CurrentState(0)
    , PendingState(0)
    , bPendingChange(0)
    , bProcessing(0)
{
}

StateMachine::~StateMachine()
{
}

int StateMachine::get_state() const
{
    // 0x062d1ad0 (ICF-shared).
    return CurrentState;
}

void StateMachine::start()
{
    // RVA 0xd3230: a virtual process() of a stack SMET_Enter event. Vtable: 0 dtor,
    // 1 process, 2 pure (OnStateEvent), 3 handle_state_change.
    SMEventData EnterEvent(SMET_Enter);
    process(&EnterEvent);
}

void StateMachine::reset()
{
    CurrentState = 0;
    PendingState = 0;
}

void StateMachine::set_state(int NewState)
{
    if (NewState != CurrentState)
    {
        PendingState = NewState;
        bPendingChange = 1;
    }
}

// .rdata:063a4978 -- the -1 "any state" value process() offers slot 2 when the current state
// did not take the event.
static const int GStateMachineAnyState = -1;

void StateMachine::process(const SMEventData* Event)
{
    // FUN_063339c0
    bProcessing = 1;
    if (!OnStateEvent(Event, CurrentState))
        OnStateEvent(Event, GStateMachineAnyState);
    handle_state_change();
    bProcessing = 0;
}

void StateMachine::handle_state_change()
{
    // FUN_06333a00: Exit against the old state, commit, then Enter against the new state. A
    // handler that calls set_state again re-arms the loop.
    while (bPendingChange != 0)
    {
        SMEventData Event(SMET_Exit);
        OnStateEvent(&Event, CurrentState);
        CurrentState = PendingState;
        bPendingChange = 0;
        Event.EventType = SMET_Enter;
        OnStateEvent(&Event, CurrentState);
    }
}

// ===========================================================================
// MessageHandler
// ===========================================================================

MessageHandler::MessageHandler(IMessageRouter* InRouter)
    : Router(InRouter)
{
    // FUN_063333a0. No null check in retail.
    HandlerId = Router->RegisterHandler(this);
}

MessageHandler::~MessageHandler()
{
    // FUN_063333f0. No null check in retail.
    Router->UnregisterHandler(this);
}

unsigned int MessageHandler::get_id() const
{
    // 0x062a66a0 (ICF-shared).
    return HandlerId;
}

void MessageHandler::send_immediate_message(StateMachineMessage* Msg, unsigned int Target)
{
    // FUN_06333410
    Router->RouteMessage(Msg, 0.0f, Target, HandlerId, -1);
}

void MessageHandler::send_delayed_message(StateMachineMessage* Msg, float Delay, unsigned int Target)
{
    // FUN_06333430
    Router->RouteMessage(Msg, Delay, Target, HandlerId, -1);
}

void MessageHandler::send_delayed_message_to_me(StateMachineMessage* Msg, float Delay, int Scope)
{
    // FUN_06333460
    Router->RouteMessage(Msg, Delay, HandlerId, HandlerId, Scope);
}

// ===========================================================================
// StateMachineMessageHandler
// ===========================================================================

StateMachineMessageHandler::StateMachineMessageHandler(IMessageRouter* InRouter)
    : StateMachine()
    , MessageHandler(InRouter)
{
    // RVA 0xd3080: the MessageHandler sub-object (+0x10) registers itself in its own ctor.
}

void StateMachineMessageHandler::receive_message(StateMachineMessage* Msg)
{
    // RVA 0xd3480: a stack SMMessage through the virtual process(). No null check on Msg.
    SMMessage Event(Msg);
    process(&Event);
}

void StateMachineMessageHandler::send_delayed_message_to_me(
    StateMachineMessage* Msg, float Delay, SM_MESSAGE_SCOPE Scope, int TargetId)
{
    // FUN_063334b0. No null check on the router; a scope outside 0..2 does nothing.
    switch (Scope)
    {
    case SMMS_Self:
        // The StateMachine base's CurrentState.
        Router->RouteMessage(Msg, Delay, HandlerId, HandlerId, CurrentState);
        break;
    case SMMS_Broadcast:
        Router->RouteMessage(Msg, Delay, HandlerId, HandlerId, -1);
        break;
    case SMMS_Target:
        Router->RouteMessage(Msg, Delay, HandlerId, HandlerId, TargetId);
        break;
    }
}

// ===========================================================================
// StateMachineGroup
// ===========================================================================

StateMachineGroup::StateMachineGroup(IMessageRouter* InRouter)
    : StateMachineMessageHandler(InRouter)
{
    // FUN_06333530: the base, then two empty std::lists. The 1 / 2 bytes stored near them are
    // the lists' allocator bytes (from the EH state counter's stack slots), not fields.
}

StateMachineGroup::~StateMachineGroup()
{
    // FUN_06333610: front to back, delete each machine (null-tested) and erase its node; then
    // the members and the MessageHandler base (which unregisters).
    FMachineList::iterator It = Machines.begin();
    while (It != Machines.end())
    {
        delete *It;
        It = Machines.erase(It);
    }
}

void StateMachineGroup::process(const SMEventData* Event)
{
    // FUN_06333840: StateMachine::process inlined, a dying-machine pass, then -- unless the
    // event is a user event -- every machine processes it and a second pass follows. No null
    // checks on the event or the machines.
    bProcessing = 1;
    if (!OnStateEvent(Event, CurrentState))
        OnStateEvent(Event, GStateMachineAnyState);
    handle_state_change();
    bProcessing = 0;

    remove_dying_machines();

    if (Event->get_event_type() == SMET_Message)
        return;

    for (FMachineList::iterator It = Machines.begin(); It != Machines.end(); ++It)
        (*It)->process(Event);

    remove_dying_machines();
}

void StateMachineGroup::add_state_machine(StateMachineMessageHandler* Handler)
{
    // FUN_06333730. No null test of Handler.
    Machines.push_back(Handler);
    SMEventData EnterEvent(SMET_Enter);
    Handler->process(&EnterEvent);
}

void StateMachineGroup::remove_state_machine(const StateMachineMessageHandler* Handler)
{
    // FUN_063337a0: only marks the machine; remove_dying_machines deletes it, so a marked
    // machine can still run in this tick's pass.
    for (FMachineList::iterator It = Machines.begin(); It != Machines.end(); ++It)
    {
        if (*It == Handler)
        {
            DyingMachines.push_back(It);
            return;
        }
    }
}

void StateMachineGroup::remove_state_machine(unsigned int Id)
{
    // FUN_06333800: the null-preserving down-cast from the MessageHandler sub-object (+0x10);
    // the virtual call is made even for null. No null test of the router.
    MessageHandler* const Registered = Router->LookupHandlerById(Id);
    remove_state_machine(static_cast<const StateMachineMessageHandler*>(Registered));
}

void StateMachineGroup::remove_dying_machines()
{
    // FUN_06333950, also inlined twice in process.
    std::list<FMachineList::iterator>::iterator It = DyingMachines.begin();
    while (It != DyingMachines.end())
    {
        delete **It;
        Machines.erase(*It);
        It = DyingMachines.erase(It);
    }
}
