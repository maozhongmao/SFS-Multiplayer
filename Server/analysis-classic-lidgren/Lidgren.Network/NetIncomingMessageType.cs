namespace Lidgren.Network;

public enum NetIncomingMessageType
{
	Error = 0,
	StatusChanged = 1,
	UnconnectedData = 2,
	ConnectionApproval = 4,
	Data = 8,
	Receipt = 0x10,
	DiscoveryRequest = 0x20,
	DiscoveryResponse = 0x40,
	VerboseDebugMessage = 0x80,
	DebugMessage = 0x100,
	WarningMessage = 0x200,
	ErrorMessage = 0x400,
	NatIntroductionSuccess = 0x800,
	ConnectionLatencyUpdated = 0x1000
}
