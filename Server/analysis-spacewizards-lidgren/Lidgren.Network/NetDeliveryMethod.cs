namespace Lidgren.Network;

public enum NetDeliveryMethod : byte
{
	Unknown = 0,
	Unreliable = 1,
	UnreliableSequenced = 2,
	ReliableUnordered = 34,
	ReliableSequenced = 35,
	ReliableOrdered = 67
}
