# Lidgren.Network 1.0.2

- Upstream: https://github.com/lidgren/lidgren-network-gen3
- NuGet: https://www.nuget.org/packages/Lidgren.Network/1.0.2
- License: MIT (see `LICENSE`)
- Binary: `Lidgren.Network.dll`, 122880 bytes
- SHA-256: `25a1cbc64bc75497dc6c67fe8a3246529c762271c68f8ad4c2864f8973e6cfbd`

This exact binary also ships in the local archived SFS 1.5 multiplayer bundle at
`Mods/Lidgren.Network/Lidgren.Network.dll`. It is pinned here so the protocol and
server projects build against the exact client-compatible assembly without relying
on the NuGet package's outdated net451-only target metadata. The published Windows
server embeds dependencies into a single executable.
