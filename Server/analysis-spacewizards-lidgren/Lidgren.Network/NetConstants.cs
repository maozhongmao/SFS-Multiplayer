namespace Lidgren.Network;

internal static class NetConstants
{
	internal const int NumTotalChannels = 99;

	internal const int NetChannelsPerDeliveryMethod = 32;

	internal const int NumSequenceNumbers = 1024;

	internal const int HeaderByteSize = 5;

	internal const int UnreliableWindowSize = 128;

	internal const int ReliableOrderedWindowSize = 64;

	internal const int ReliableSequencedWindowSize = 64;

	internal const int DefaultWindowSize = 64;

	internal const int MaxFragmentationGroups = 65534;

	internal const int UnfragmentedMessageHeaderSize = 5;

	internal const int NumSequencedChannels = 97;

	internal const int NumReliableChannels = 65;

	internal const string ConnResetMessage = "Connection was reset by remote host";
}
