// multiplayersfs probe #2: reach libil2cpp from our injected .so (no libc link; @extern + lazy binding)
const RTLD_NOW: c_int = 2;
const FnDlopen = *const fn ([*:0]const u8, c_int) callconv(.c) ?*anyopaque;
const FnDlsym = *const fn (?*anyopaque, [*:0]const u8) callconv(.c) ?*anyopaque;
const FnOpen = *const fn ([*:0]const u8, c_int, ...) callconv(.c) c_int;
const FnWrite = *const fn (c_int, [*]const u8, usize) callconv(.c) isize;
const FnClose = *const fn (c_int) callconv(.c) c_int;
const FnDomainGet = *const fn () callconv(.c) ?*anyopaque;
const dlopen = @extern(FnDlopen, .{ .name = "dlopen" });
const dlsym = @extern(FnDlsym, .{ .name = "dlsym" });
const open = @extern(FnOpen, .{ .name = "open" });
const write = @extern(FnWrite, .{ .name = "write" });
const close = @extern(FnClose, .{ .name = "close" });
const MARKER = "/sdcard/Android/data/com.StefMorojna.SpaceflightSimulator/files/sfsmp-probe.txt";

fn logOut(msg: []const u8) void {
    const fd = open(MARKER, 0x241, @as(c_int, 0o644));
    if (fd < 0) return;
    _ = write(fd, msg.ptr, msg.len);
    _ = close(fd);
}
fn put(buf: []u8, n: *usize, s: []const u8) void {
    for (s) |ch| { if (n.* < buf.len) { buf[n.*] = ch; n.* += 1; } }
}
export fn JNI_OnLoad(vm: ?*anyopaque, reserved: ?*anyopaque) callconv(.c) c_int {
    _ = vm; _ = reserved;
    var out: [160]u8 = undefined;
    var n: usize = 0;
    put(&out, &n, "probe2: ");
    var h: ?*anyopaque = null;
    var tries: usize = 0;
    while (tries < 400000) : (tries += 1) {
        h = dlopen("libil2cpp.so", RTLD_NOW);
        if (h != null) break;
    }
    if (h == null) { put(&out, &n, "libil2cpp=NO\n"); logOut(out[0..n]); return 0x00010006; }
    put(&out, &n, "libil2cpp=OK ");
    const p = dlsym(h, "il2cpp_domain_get");
    if (p != null) {
        put(&out, &n, "domain_get=OK ");
        const f: FnDomainGet = @ptrCast(@alignCast(p.?));
        const d = f();
        if (d != null) put(&out, &n, "domain=OK\n") else put(&out, &n, "domain=NULL\n");
    } else { put(&out, &n, "domain_get=NO\n"); }
    logOut(out[0..n]);
    return 0x00010006;
}
