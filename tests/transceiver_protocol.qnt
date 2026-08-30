// ESP-NOW transceiver call, PTT floor, and parent-authoritative conversation mode model.
// Device A denotes the lower station MAC used by the simultaneous-call tie-breaker.
module transceiver_protocol{
    type Message = {
        kind:str,
        sender:str,
        receiver:str,
        session: int,
    }

    type State = {
        aPhase:str,
        bPhase:str,
        aRole:str,
        bRole:str,
        aSession: int,
        bSession: int,
        aRetiredFromB: int,
        bRetiredFromA: int,
        aStarted: bool,
        bStarted: bool,
        floor:str,
        aMode:str,
        bMode:str,
        aRestarted: bool,
        bRestarted: bool,
        network:Set[Message],
    }

    var st:State

        action init = st' = { aPhase: "Off",
    bPhase: "Off",
    aRole: "None",
    bRole: "None",
    aSession: 0,
    bSession: 0,
    aRetiredFromB: 0,
    bRetiredFromA: 0,
    aStarted: false,
    bStarted: false,
    floor: "None",
    aMode: "Ptt",
    bMode: "Ptt",
    aRestarted: false,
    bRestarted: false,
    network:Set(),
}

action startA = all{
    st.aPhase == "Off",
    not(st.aStarted),
    st' = { ...st, aPhase: "Discovering",
    aStarted: true,
    network:st.network.union(Set({kind: "Discover", sender: "A", receiver: "B", session: 0})),
},
}

action startB = all{
    st.bPhase == "Off",
    not(st.bStarted),
    st' = { ...st, bPhase : "Discovering",
    bStarted : true,
    network : st.network.union(Set({kind : "Discover", sender : "B", receiver : "A", session : 0})),
},
}

action deliverDiscoverAToB = all{
    {kind : "Discover", sender : "A", receiver : "B", session : 0}.in(st.network),
    st.bPhase == "Discovering",
    st' = { ...st, bPhase : "Calling",
    bRole : "Parent",
    bSession : 2,
    network : st.network.union(Set({kind : "Call", sender : "B", receiver : "A", session : 2})),
},
}

action deliverDiscoverBToA = all{
    {kind : "Discover", sender : "B", receiver : "A", session : 0}.in(st.network),
    st.aPhase == "Discovering",
    st' = { ...st, aPhase : "Calling",
    aRole : "Parent",
    aSession : 1,
    network : st.network.union(Set({kind : "Call", sender : "A", receiver : "B", session : 1})),
},
}

action deliverCallAToB = all{
    {kind : "Call", sender : "A", receiver : "B", session : 1}.in(st.network),
    st.bRetiredFromA != 1,
    st.bPhase == "Discovering" or st.bPhase == "Calling",
    st' = { ...st, bPhase : "Answering",
    bRole : "Child",
    bSession : 1,
    floor : "None",
    network : st.network.union(Set({kind : "Answer", sender : "B", receiver : "A", session : 1})),
},
}

action deliverCallBToA = all{
    {kind : "Call", sender : "B", receiver : "A", session : 2}.in(st.network),
    st.aRetiredFromB != 2,
    st.aPhase == "Discovering",
    st' = { ...st, aPhase : "Answering",
    aRole : "Child",
    aSession : 2,
    floor : "None",
    network : st.network.union(Set({kind : "Answer", sender : "A", receiver : "B", session : 2})),
},
}

action observeLosingCallAtA = all{{kind : "Call", sender : "B", receiver : "A", session : 2}.in(st.network),
                                  st.aPhase == "Calling",
                                  st' = { ...st, aRetiredFromB: 2 }, }

action duplicateOrReorderDelivery = all{st.network.size() > 0, st' = st, }

action dropMessage = nondet message = st.network.oneOf() st' = { ...st, network: st.network.filter(m => m != message) }

                                      action deliverAnswerToA = all{
    {kind : "Answer", sender : "B", receiver : "A", session : st.aSession}.in(st.network),
    st.aPhase == "Calling",
    st' = { ...st, aPhase : "Connected",
    network : st.network.union(Set({kind : "Confirm", sender : "A", receiver : "B", session : st.aSession})),
},
}

action deliverAnswerToB = all{
    {kind : "Answer", sender : "A", receiver : "B", session : st.bSession}.in(st.network),
    st.bPhase == "Calling",
    st' = { ...st, bPhase : "Connected",
    network : st.network.union(Set({kind : "Confirm", sender : "B", receiver : "A", session : st.bSession})),
},
}

action deliverConfirmToA = all{{kind : "Confirm", sender : "B", receiver : "A", session : st.aSession}.in(st.network),
                               st.aPhase == "Answering",
                               st' = { ...st, aPhase: "Connected" }, }

action deliverConfirmToB = all{{kind : "Confirm", sender : "A", receiver : "B", session : st.bSession}.in(st.network),
                               st.bPhase == "Answering",
                               st' = { ...st, bPhase: "Connected" }, }

action parentATalks = all{st.aPhase == "Connected", st.aRole == "Parent", st' = { ...st, floor: "A" }, }

action parentBTalks = all{st.bPhase == "Connected", st.bRole == "Parent", st' = { ...st, floor: "B" }, }

action childATalks = all{st.aPhase == "Connected", st.aRole == "Child", st.floor != "B", st' = { ...st, floor: "A" }, }

action childBTalks = all{st.bPhase == "Connected", st.bRole == "Child", st.floor != "A", st' = { ...st, floor: "B" }, }

action releaseFloor = all{st.floor != "None", st' = { ...st, floor: "None" }, }

// The parent is the single mode authority. A child tap is a request to the
// parent; the action below represents the committed ModeSet after delivery.
action setOpenMic =
    all{st.aPhase == "Connected", st.bPhase == "Connected", st.aRole == "Parent" or st.bRole == "Parent",
        st' = { ...st, aMode: "OpenMic", bMode: "OpenMic", floor: "None" }, }

action setPtt = all{st.aPhase == "Connected", st.bPhase == "Connected", st.aRole == "Parent" or st.bRole == "Parent",
                    st' = { ...st, aMode: "Ptt", bMode: "Ptt", floor: "None" }, }

action restartA = all
{
    st.aPhase == "Connected", st' = { ...st, aPhase : "Discovering",
                                                      aRole : "None",
                                                              aSession : 0,
                                                              aMode : "Ptt",
                                                                      aRestarted
        : true,
          floor : if (st.floor == "A") "None" else st.floor,
          network : st.network.union(Set({kind : "Discover", sender : "A", receiver : "B", session : 0})),
}
,
}

action restartB = all
{
    st.bPhase == "Connected", st' = { ...st, bPhase : "Discovering",
                                                      bRole : "None",
                                                              bSession : 0,
                                                              bMode : "Ptt",
                                                                      bRestarted
        : true,
          floor : if (st.floor == "B") "None" else st.floor,
          network : st.network.union(Set({kind : "Discover", sender : "B", receiver : "A", session : 0})),
}
,
}

action observeRestartedAAtB = all{
    {kind : "Discover", sender : "A", receiver : "B", session : 0}.in(st.network),
    st.bPhase == "Connected",
    st' = { ...st, bPhase : "Discovering",
    bRole : "None",
    bSession : 0,
    bMode : "Ptt",
    bRetiredFromA : st.bSession,
    floor : "None",
    network : st.network.union(Set({kind : "Discover", sender : "B", receiver : "A", session : 0})),
},
}

action observeRestartedBAtA = all{
    {kind : "Discover", sender : "B", receiver : "A", session : 0}.in(st.network),
    st.aPhase == "Connected",
    st' = { ...st, aPhase : "Discovering",
    aRole : "None",
    aSession : 0,
    aMode : "Ptt",
    aRetiredFromB : st.aSession,
    floor : "None",
    network : st.network.union(Set({kind : "Discover", sender : "A", receiver : "B", session : 0})),
},
}

action hangupA = all
{
    st.aPhase != "Off", st' = { ...st, aPhase : "Off",
                                                aRole : "None",
                                                        aSession : 0,
                                                        aMode
        : "Ptt",
          aRetiredFromB : if (st.aRole == "Child") st.aSession else st.aRetiredFromB,
          floor : if (st.floor == "A") "None" else st.floor,
          network : st.network.union(Set({kind : "Hangup", sender : "A", receiver : "B", session : st.aSession})),
}
,
}

action hangupB = all
{
    st.bPhase != "Off", st' = { ...st, bPhase : "Off",
                                                bRole : "None",
                                                        bSession : 0,
                                                        bMode
        : "Ptt",
          bRetiredFromA : if (st.bRole == "Child") st.bSession else st.bRetiredFromA,
          floor : if (st.floor == "B") "None" else st.floor,
          network : st.network.union(Set({kind : "Hangup", sender : "B", receiver : "A", session : st.bSession})),
}
,
}

action deliverHangupToA = nondet message = st.network.filter(m => m.kind == "Hangup" and m.receiver == "A").oneOf() all
{
    st.aSession == message.session,
        st' = { ...st, aPhase : "Off",
                                aRole : "None",
                                        aSession : 0,
                                        aMode : "Ptt",
                                                aRetiredFromB : message.session,
                                                floor : if (st.floor == "A") "None" else st.floor,
}
,
}

action deliverHangupToB = nondet message = st.network.filter(m => m.kind == "Hangup" and m.receiver == "B").oneOf() all
{
    st.bSession == message.session,
        st' = { ...st, bPhase : "Off",
                                bRole : "None",
                                        bSession : 0,
                                        bMode : "Ptt",
                                                bRetiredFromA : message.session,
                                                floor : if (st.floor == "B") "None" else st.floor,
}
,
}

action step =
    any{
        startA,
        startB,
        deliverDiscoverAToB,
        deliverDiscoverBToA,
        deliverCallAToB,
        deliverCallBToA,
        observeLosingCallAtA,
        deliverAnswerToA,
        deliverAnswerToB,
        deliverConfirmToA,
        deliverConfirmToB,
        parentATalks,
        parentBTalks,
        childATalks,
        childBTalks,
        releaseFloor,
        setOpenMic,
        setPtt,
        restartA,
        restartB,
        observeRestartedAAtB,
        observeRestartedBAtA,
        duplicateOrReorderDelivery,
        dropMessage,
        hangupA,
        hangupB,
        deliverHangupToA,
        deliverHangupToB,
    }

def complementaryRoles = (st.aPhase == "Connected" and st.bPhase == "Connected") implies(
    (st.aRole == "Parent" and st.bRole == "Child") or (st.aRole == "Child" and st.bRole == "Parent"))

    def connectedSessionMatches =
        (st.aPhase == "Connected" and st.bPhase == "Connected") implies st.aSession == st.bSession

                                                                                           def incomingIsChild =
            and{
                st.aPhase == "Incoming" implies st.aRole == "Child",
                st.bPhase == "Incoming" implies st.bRole == "Child",
                st.aPhase == "Answering" implies st.aRole == "Child",
                st.bPhase == "Answering" implies st.bRole == "Child",
            }

            def floorHasConnectedOwner =
                (st.floor == "A" implies st.aPhase == "Connected") and
                (st.floor == "B" implies st.bPhase == "Connected")

                    def modesAreValid =
                    (st.aMode == "Ptt" or st.aMode == "OpenMic") and (st.bMode == "Ptt" or st.bMode == "OpenMic")

                                                                         def safety =
                        complementaryRoles and connectedSessionMatches and incomingIsChild and floorHasConnectedOwner and modesAreValid

                            def connectedWitness =
                                st.aPhase == "Connected" and st.bPhase ==
                                "Connected"

                                def reconnectedAfterRestartWitness =
                                    connectedWitness and (st.aRestarted or st.bRestarted)

                                                             def openMicWitness =
                                        connectedWitness and st.aMode == "OpenMic" and st.bMode == "OpenMic"
}
