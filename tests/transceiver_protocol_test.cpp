#include "../main/apps/app_transceiver/model/protocol.h"
#include <cassert>

using namespace transceiver;

int main()
{
    const MacAddress lower = {0x28, 0x84, 0x85, 0x43, 0xA2, 0x28};
    const MacAddress upper = {0x28, 0x84, 0x85, 0x43, 0xB2, 0x54};

    Protocol waiting(lower);
    waiting.startDiscovery();
    assert(waiting.phase() == Phase::Discovering);
    waiting.receiveCall(upper, 7);
    assert(waiting.phase() == Phase::Incoming);
    assert(waiting.role() == Role::Child);

    Protocol parent(lower);
    Protocol child(upper);
    parent.startCall(11);
    child.receiveCall(lower, 11);
    assert(parent.phase() == Phase::Calling);
    assert(parent.role() == Role::Parent);
    assert(child.phase() == Phase::Incoming);
    assert(child.role() == Role::Child);
    assert(child.answer());
    assert(child.phase() == Phase::Answering);
    parent.receiveAnswer(upper, 11);
    assert(parent.phase() == Phase::Connected);
    child.receiveConfirm(lower, 11);
    assert(child.phase() == Phase::Connected);
    assert(parent.conversationMode() == ConversationMode::Ptt);
    assert(child.conversationMode() == ConversationMode::Ptt);
    assert(parent.setConversationMode(ConversationMode::OpenMic));
    assert(child.setConversationMode(ConversationMode::OpenMic));
    assert(parent.acceptAudio(upper, 11, Role::Child));
    assert(child.acceptAudio(lower, 11, Role::Parent));
    assert(!parent.beginLocalTalk());
    assert(parent.setConversationMode(ConversationMode::Ptt));
    assert(child.setConversationMode(ConversationMode::Ptt));

    assert(!parent.receivePeerRestart(lower));
    assert(parent.receivePeerRestart(upper));
    assert(parent.phase() == Phase::Discovering);
    assert(parent.role() == Role::None);
    assert(!parent.hasPeer());

    Protocol restartedChild(upper);
    restartedChild.startDiscovery();
    restartedChild.startCall(12);
    parent.receiveCall(upper, 12);
    assert(parent.phase() == Phase::Incoming);
    assert(parent.answer());
    restartedChild.receiveAnswer(lower, 12);
    parent.receiveConfirm(upper, 12);
    assert(parent.phase() == Phase::Connected);
    assert(parent.role() == Role::Child);
    assert(restartedChild.phase() == Phase::Connected);
    assert(restartedChild.role() == Role::Parent);

    assert(parent.beginLocalTalk());
    restartedChild.receiveTalkStart(lower, 12, Role::Child);
    assert(restartedChild.remoteTalking());
    assert(!restartedChild.localTalking());
    assert(restartedChild.beginLocalTalk());
    parent.receiveTalkStart(upper, 12, Role::Parent);
    assert(parent.remoteTalking());
    assert(!parent.localTalking());
    assert(restartedChild.localTalking());
    assert(!parent.beginLocalTalk());

    parent.receiveHangup(upper, 999);
    assert(parent.phase() == Phase::Connected);
    parent.receiveHangup(upper, 12);
    assert(parent.phase() == Phase::Off);
    assert(parent.conversationMode() == ConversationMode::Ptt);
    parent.receiveCall(upper, 12);
    assert(parent.phase() == Phase::Off);

    Protocol simultaneousLower(lower);
    Protocol simultaneousUpper(upper);
    simultaneousLower.startDiscovery();
    simultaneousUpper.startDiscovery();
    simultaneousLower.startCall(21);
    simultaneousUpper.startCall(22);
    simultaneousLower.receiveCall(upper, 22);
    simultaneousUpper.receiveCall(lower, 21);
    assert(simultaneousLower.phase() == Phase::Calling);
    assert(simultaneousLower.role() == Role::Parent);
    assert(simultaneousUpper.phase() == Phase::Incoming);
    assert(simultaneousUpper.role() == Role::Child);
    simultaneousLower.hangup();
    simultaneousUpper.receiveHangup(lower, 21);
    assert(simultaneousUpper.phase() == Phase::Off);
    simultaneousLower.receiveCall(upper, 22);
    assert(simultaneousLower.phase() == Phase::Off);

    Protocol staleLower(lower);
    Protocol staleUpper(upper);
    staleLower.startCall(31);
    staleLower.hangup();
    staleUpper.startCall(32);
    staleLower.receiveCall(upper, 32);
    staleUpper.receiveCall(lower, 31);
    assert(staleLower.answer());
    assert(staleUpper.answer());
    staleLower.receiveAnswer(upper, 32);
    staleUpper.receiveAnswer(lower, 31);
    assert(staleLower.phase() == Phase::Answering);
    assert(staleUpper.phase() == Phase::Answering);

    return 0;
}
