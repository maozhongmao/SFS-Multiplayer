namespace Lidgren.Network;

public enum NetConnectionStatus
{
	None,
	InitiatedConnect,
	ReceivedInitiation,
	RespondedAwaitingApproval,
	RespondedConnect,
	Connected,
	Disconnecting,
	Disconnected
}
