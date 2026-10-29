package pl.tajchert.slowik.stream

enum class StageState(val label: String) {
    NO_RECEIVER("NO RECEIVER"),
    READY("READY"),
    LIVE("LIVE"),
    MUTED("MUTED"),
}

/** State rules from docs/PROTOCOL.md ("Phone UI state derived from the protocol"). */
fun deriveStageState(
    streaming: Boolean,
    receiverConnected: Boolean,
    live: Boolean,
    operatorMute: Boolean,
    localMute: Boolean,
): StageState = when {
    !streaming || !receiverConnected -> StageState.NO_RECEIVER
    localMute || operatorMute -> StageState.MUTED
    live -> StageState.LIVE
    else -> StageState.READY
}
