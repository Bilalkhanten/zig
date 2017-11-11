const std = @import("index.zig");
const builtin = @import("builtin");
const Os = builtin.Os;
const system = switch(builtin.os) {
    Os.linux => @import("os/linux.zig"),
    Os.darwin, Os.macosx, Os.ios => @import("os/darwin.zig"),
    Os.windows => @import("os/windows/index.zig"),
    else => @compileError("Unsupported OS"),
};
const c = std.c;

const math = std.math;
const debug = std.debug;
const assert = debug.assert;
const os = std.os;
const mem = std.mem;
const Buffer = std.Buffer;
const fmt = std.fmt;

const is_posix = builtin.os != builtin.Os.windows;
const is_windows = builtin.os == builtin.Os.windows;

test "import io tests" {
    comptime {
        _ = @import("io_test.zig");
    }
}

/// The function received invalid input at runtime. An Invalid error means a
/// bug in the program that called the function.
error Invalid;

error DiskQuota;
error FileTooBig;
error Io;
error NoSpaceLeft;
error BadPerm;
error BrokenPipe;
error BadFd;
error IsDir;
error NotDir;
error SymLinkLoop;
error ProcessFdQuotaExceeded;
error SystemFdQuotaExceeded;
error NameTooLong;
error NoDevice;
error PathNotFound;
error OutOfMemory;
error Unseekable;
error EndOfFile;

pub fn getStdErr() -> %File {
    const handle = if (is_windows) {
        %return os.windowsGetStdHandle(system.STD_ERROR_HANDLE)
    } else if (is_posix) {
        system.STDERR_FILENO
    } else {
        unreachable
    };
    return File.openHandle(handle);
}

pub fn getStdOut() -> %File {
    const handle = if (is_windows) {
        %return os.windowsGetStdHandle(system.STD_OUTPUT_HANDLE)
    } else if (is_posix) {
        system.STDOUT_FILENO
    } else {
        unreachable
    };
    return File.openHandle(handle);
}

pub fn getStdIn() -> %File {
    const handle = if (is_windows) {
        %return os.windowsGetStdHandle(system.STD_INPUT_HANDLE)
    } else if (is_posix) {
        system.STDIN_FILENO
    } else {
        unreachable
    };
    return File.openHandle(handle);
}

/// Implementation of InStream trait for File
pub const FileInStream = struct {
    file: &File,
    stream: InStream,

    pub fn init(file: &File) -> FileInStream {
        return FileInStream {
            .file = file,
            .stream = InStream {
                .readFn = readFn,
            },
        };
    }

    fn readFn(in_stream: &InStream, buffer: []u8) -> %usize {
        const self = @fieldParentPtr(FileInStream, "stream", in_stream);
        return self.file.read(buffer);
    }
};

/// Implementation of OutStream trait for File
pub const FileOutStream = struct {
    file: &File,
    stream: OutStream,

    pub fn init(file: &File) -> FileOutStream {
        return FileOutStream {
            .file = file,
            .stream = OutStream {
                .writeFn = writeFn,
            },
        };
    }

    fn writeFn(out_stream: &OutStream, bytes: []const u8) -> %void {
        const self = @fieldParentPtr(FileOutStream, "stream", out_stream);
        return self.file.write(bytes);
    }
};

/// Implementation of OutStream trait for Buffer
pub const BufferOutStream = struct {
    buffer: &Buffer,
    stream: OutStream,

    pub fn init(buffer: &Buffer) -> BufferOutStream {
        return BufferOutStream {
            .buffer = buffer,
            .stream = OutStream {
                .writeFn = writeFn,
            },
        };
    }

    fn writeFn(out_stream: &OutStream, bytes: []const u8) -> %void {
        const self = @fieldParentPtr(BufferOutStream, "stream", out_stream);
        return self.buffer.append(bytes);
    }
};

/// Implementation of InStream trait for Buffer
pub const BufferInStream = struct {
    buffer: &Buffer,
    index: usize,
    stream: InStream,

    pub fn init(buffer: &Buffer) -> BufferInStream {
        return BufferInStream {
            .buffer = buffer,
            .index = 0,
            .stream = InStream {
                .readFn = readFn,
            },
        };
    }

    fn readFn(in_stream: &InStream, bytes: []const u8) -> %usize {
        const self = @fieldParentPtr(BufferInStream, "stream", in_stream);
        const amt_to_read = math.min(bytes.len, self.buffer.len() - self.index);
        mem.copy(u8, bytes, self.buffer.toSliceConst(self.buffer)[self.index..self.index + amt_to_read];
        return amt_to_read;
    }
};

pub const File = struct {
    /// The OS-specific file descriptor or file handle.
    handle: os.FileHandle,

    /// `path` may need to be copied in memory to add a null terminating byte. In this case
    /// a fixed size buffer of size std.os.max_noalloc_path_len is an attempted solution. If the fixed
    /// size buffer is too small, and the provided allocator is null, error.NameTooLong is returned.
    /// otherwise if the fixed size buffer is too small, allocator is used to obtain the needed memory.
    /// Call close to clean up.
    pub fn openRead(path: []const u8, allocator: ?&mem.Allocator) -> %File {
        if (is_posix) {
            const flags = system.O_LARGEFILE|system.O_RDONLY;
            const fd = %return os.posixOpen(path, flags, 0, allocator);
            return openHandle(fd);
        } else if (is_windows) {
            const handle = %return os.windowsOpen(path, system.GENERIC_READ, system.FILE_SHARE_READ,
                system.OPEN_EXISTING, system.FILE_ATTRIBUTE_NORMAL, allocator);
            return openHandle(handle);
        } else {
            unreachable;
        }
    }

    /// Calls `openWriteMode` with 0o666 for the mode.
    pub fn openWrite(path: []const u8, allocator: ?&mem.Allocator) -> %File {
        return openWriteMode(path, 0o666, allocator);

    }

    /// `path` may need to be copied in memory to add a null terminating byte. In this case
    /// a fixed size buffer of size std.os.max_noalloc_path_len is an attempted solution. If the fixed
    /// size buffer is too small, and the provided allocator is null, error.NameTooLong is returned.
    /// otherwise if the fixed size buffer is too small, allocator is used to obtain the needed memory.
    /// Call close to clean up.
    pub fn openWriteMode(path: []const u8, mode: usize, allocator: ?&mem.Allocator) -> %File {
        if (is_posix) {
            const flags = system.O_LARGEFILE|system.O_WRONLY|system.O_CREAT|system.O_CLOEXEC|system.O_TRUNC;
            const fd = %return os.posixOpen(path, flags, mode, allocator);
            return openHandle(fd);
        } else if (is_windows) {
            const handle = %return os.windowsOpen(path, system.GENERIC_WRITE,
                system.FILE_SHARE_WRITE|system.FILE_SHARE_READ|system.FILE_SHARE_DELETE,
                system.CREATE_ALWAYS, system.FILE_ATTRIBUTE_NORMAL, allocator);
            return openHandle(handle);
        } else {
            unreachable;
        }

    }

    pub fn openHandle(handle: os.FileHandle) -> File {
        return File {
            .handle = handle,
        };
    }


    /// Upon success, the stream is in an uninitialized state. To continue using it,
    /// you must use the open() function.
    pub fn close(self: &File) {
        os.close(self.handle);
        self.handle = undefined;
    }

    /// Calls `os.isTty` on `self.handle`.
    pub fn isTty(self: &File) -> bool {
        return os.isTty(self.handle);
    }

    pub fn seekForward(self: &File, amount: isize) -> %void {
        switch (builtin.os) {
            Os.linux, Os.darwin => {
                const result = system.lseek(self.handle, amount, system.SEEK_CUR);
                const err = system.getErrno(result);
                if (err > 0) {
                    return switch (err) {
                        system.EBADF => error.BadFd,
                        system.EINVAL => error.Unseekable,
                        system.EOVERFLOW => error.Unseekable,
                        system.ESPIPE => error.Unseekable,
                        system.ENXIO => error.Unseekable,
                        else => os.unexpectedErrorPosix(err),
                    };
                }
            },
            else => @compileError("unsupported OS"),
        }
    }

    pub fn seekTo(self: &File, pos: usize) -> %void {
        switch (builtin.os) {
            Os.linux, Os.darwin => {
                const result = system.lseek(self.handle, @bitCast(isize, pos), system.SEEK_SET);
                const err = system.getErrno(result);
                if (err > 0) {
                    return switch (err) {
                        system.EBADF => error.BadFd,
                        system.EINVAL => error.Unseekable,
                        system.EOVERFLOW => error.Unseekable,
                        system.ESPIPE => error.Unseekable,
                        system.ENXIO => error.Unseekable,
                        else => os.unexpectedErrorPosix(err),
                    };
                }
            },
            else => @compileError("unsupported OS"),
        }
    }

    pub fn getPos(self: &File) -> %usize {
        switch (builtin.os) {
            Os.linux, Os.darwin => {
                const result = system.lseek(self.handle, 0, system.SEEK_CUR);
                const err = system.getErrno(result);
                if (err > 0) {
                    return switch (err) {
                        system.EBADF => error.BadFd,
                        system.EINVAL => error.Unseekable,
                        system.EOVERFLOW => error.Unseekable,
                        system.ESPIPE => error.Unseekable,
                        system.ENXIO => error.Unseekable,
                        else => os.unexpectedErrorPosix(err),
                    };
                }
                return result;
            },
            else => @compileError("unsupported OS"),
        }
    }

    pub fn getEndPos(self: &File) -> %usize {
        if (is_posix) {
            var stat: system.Stat = undefined;
            const err = system.getErrno(system.fstat(self.handle, &stat));
            if (err > 0) {
                return switch (err) {
                    system.EBADF => error.BadFd,
                    system.ENOMEM => error.OutOfMemory,
                    else => os.unexpectedErrorPosix(err),
                }
            }

            return usize(stat.size);
        } else if (is_windows) {
            var file_size: system.LARGE_INTEGER = undefined;
            if (system.GetFileSizeEx(self.handle, &file_size) == 0) {
                const err = system.GetLastError();
                return switch (err) {
                    else => os.unexpectedErrorWindows(err),
                };
            }
            if (file_size < 0)
                return error.Overflow;
            return math.cast(usize, u64(file_size));
        } else {
            unreachable;
        }
    }

    pub fn read(self: &File, buffer: []u8) -> %usize {
        if (is_posix) {
            var index: usize = 0;
            while (index < buffer.len) {
                const amt_read = system.read(self.handle, &buffer[index], buffer.len - index);
                const read_err = system.getErrno(amt_read);
                if (read_err > 0) {
                    switch (read_err) {
                        system.EINTR  => continue,
                        system.EINVAL => unreachable,
                        system.EFAULT => unreachable,
                        system.EBADF  => return error.BadFd,
                        system.EIO    => return error.Io,
                        else          => return os.unexpectedErrorPosix(read_err),
                    }
                }
                if (amt_read == 0) return index;
                index += amt_read;
            }
            return index;
        } else if (is_windows) {
            var index: usize = 0;
            while (index < buffer.len) {
                const want_read_count = system.DWORD(math.min(system.DWORD(@maxValue(system.DWORD)), buffer.len - index));
                var amt_read: system.DWORD = undefined;
                if (system.ReadFile(self.handle, @ptrCast(&c_void, &buffer[index]), want_read_count, &amt_read, null) == 0) {
                    const err = system.GetLastError();
                    return switch (err) {
                        system.ERROR.OPERATION_ABORTED => continue,
                        system.ERROR.BROKEN_PIPE => return index,
                        else => os.unexpectedErrorWindows(err),
                    };
                }
                if (amt_read == 0) return index;
                index += amt_read;
            }
            return index;
        } else {
            unreachable;
        }
    }

    fn write(self: &File, bytes: []const u8) -> %void {
        if (is_posix) {
            %return os.posixWrite(self.handle, bytes);
        } else if (is_windows) {
            %return os.windowsWrite(self.handle, bytes);
        } else {
            @compileError("Unsupported OS");
        }
    }
};

error StreamTooLong;
error EndOfStream;

pub const InStream = struct {
    /// Return the number of bytes read. If the number read is smaller than buf.len, it
    /// means the stream reached the end. Reaching the end of a stream is not an error
    /// condition.
    readFn: fn(self: &InStream, buffer: []u8) -> %usize,

    /// Replaces `buffer` contents by reading from the stream until it is finished.
    /// If `buffer.len()` would exceed `max_size`, `error.StreamTooLong` is returned and
    /// the contents read from the stream are lost.
    pub fn readAllBuffer(self: &InStream, buffer: &Buffer, max_size: usize) -> %void {
        %return buffer.resize(0);

        var actual_buf_len: usize = 0;
        while (true) {
            const dest_slice = buffer.toSlice()[actual_buf_len..];
            const bytes_read = %return self.readFn(self, dest_slice);
            actual_buf_len += bytes_read;

            if (bytes_read != dest_slice.len) {
                buffer.shrink(actual_buf_len);
                return;
            }

            const new_buf_size = math.min(max_size, actual_buf_len + os.page_size);
            if (new_buf_size == actual_buf_len)
                return error.StreamTooLong;
            %return buffer.resize(new_buf_size);
        }
    }

    /// Allocates enough memory to hold all the contents of the stream. If the allocated
    /// memory would be greater than `max_size`, returns `error.StreamTooLong`.
    /// Caller owns returned memory.
    /// If this function returns an error, the contents from the stream read so far are lost.
    pub fn readAllAlloc(self: &InStream, allocator: &mem.Allocator, max_size: usize) -> %[]u8 {
        var buf = Buffer.initNull(allocator);
        defer buf.deinit();

        %return self.readAllBuffer(&buf, max_size);
        return buf.toOwnedSlice();
    }

    /// Replaces `buffer` contents by reading from the stream until `delimiter` is found.
    /// Does not include the delimiter in the result.
    /// If `buffer.len()` would exceed `max_size`, `error.StreamTooLong` is returned and the contents
    /// read from the stream so far are lost.
    pub fn readUntilDelimiterBuffer(self: &InStream, buffer: &Buffer, delimiter: u8, max_size: usize) -> %void {
        %return buf.resize(0);

        while (true) {
            var byte: u8 = %return self.readByte();

            if (byte == delimiter) {
                return;
            }

            if (buf.len() == max_size) {
                return error.StreamTooLong;
            }

            %return buf.appendByte(byte);
        }
    }

    /// Allocates enough memory to read until `delimiter`. If the allocated
    /// memory would be greater than `max_size`, returns `error.StreamTooLong`.
    /// Caller owns returned memory.
    /// If this function returns an error, the contents from the stream read so far are lost.
    pub fn readUntilDelimiterAlloc(self: &InStream, allocator: &mem.Allocator,
        delimiter: u8, max_size: usize) -> %[]u8
    {
        var buf = Buffer.initNull(allocator);
        defer buf.deinit();

        %return self.readUntilDelimiterBuffer(self, &buf, delimiter, max_size);
        return buf.toOwnedSlice();
    }

    /// Returns the number of bytes read. If the number read is smaller than buf.len, it
    /// means the stream reached the end. Reaching the end of a stream is not an error
    /// condition.
    pub fn read(self: &InStream, buffer: []u8) -> %usize {
        return self.readFn(self, buffer);
    }

    /// Same as `read` but end of stream returns `error.EndOfStream`.
    pub fn readNoEof(self: &InStream, buf: []u8) -> %void {
        const amt_read = %return self.read(buf);
        if (amt_read < buf.len) return error.EndOfStream;
    }

    /// Reads 1 byte from the stream or returns `error.EndOfStream`.
    pub fn readByte(self: &InStream) -> %u8 {
        var result: [1]u8 = undefined;
        %return self.readNoEof(result[0..]);
        return result[0];
    }

    /// Same as `readByte` except the returned byte is signed.
    pub fn readByteSigned(self: &InStream) -> %i8 {
        return @bitCast(i8, %return self.readByte());
    }

    pub fn readIntLe(self: &InStream, comptime T: type) -> %T {
        return self.readInt(false, T);
    }

    pub fn readIntBe(self: &InStream, comptime T: type) -> %T {
        return self.readInt(true, T);
    }

    pub fn readInt(self: &InStream, is_be: bool, comptime T: type) -> %T {
        var bytes: [@sizeOf(T)]u8 = undefined;
        %return self.readNoEof(bytes[0..]);
        return mem.readInt(bytes, T, is_be);
    }

    pub fn readVarInt(self: &InStream, is_be: bool, comptime T: type, size: usize) -> %T {
        assert(size <= @sizeOf(T));
        assert(size <= 8);
        var input_buf: [8]u8 = undefined;
        const input_slice = input_buf[0..size];
        %return self.readNoEof(input_slice);
        return mem.readInt(input_slice, T, is_be);
    }


};

pub const OutStream = struct {
    writeFn: fn(self: &OutStream, bytes: []const u8) -> %void,

    pub fn print(self: &OutStream, comptime format: []const u8, args: ...) -> %void {
        return std.fmt.format(self, self.writeFn, format, args);
    }

    pub fn write(self: &OutStream, bytes: []const u8) -> %void {
        return self.writeFn(self, bytes);
    }

    pub fn writeByte(self: &OutStream, byte: u8) -> %void {
        const slice = (&byte)[0..1];
        return self.writeFn(self, slice);
    }
};

/// `path` may need to be copied in memory to add a null terminating byte. In this case
/// a fixed size buffer of size `std.os.max_noalloc_path_len` is an attempted solution. If the fixed
/// size buffer is too small, and the provided allocator is null, `error.NameTooLong` is returned.
/// otherwise if the fixed size buffer is too small, allocator is used to obtain the needed memory.
pub fn writeFile(path: []const u8, data: []const u8, allocator: ?&mem.Allocator) -> %void {
    var file = %return File.openWrite(path, allocator);
    defer file.close();
    %return file.write(data);
}

pub const BufferedInStream = BufferedInStreamCustom(os.page_size);

pub fn BufferedInStreamCustom(comptime buffer_size: usize) -> type {
    return struct {
        const Self = this;

        pub stream: InStream,

        unbuffered_in_stream: &InStream,

        buffer: [buffer_size]u8,
        start_index: usize,
        end_index: usize,

        pub fn init(unbuffered_in_stream: &InStream) -> Self {
            return Self {
                .unbuffered_in_stream = unbuffered_in_stream,
                .buffer = undefined,

                // Initialize these two fields to buffer_size so that
                // in `readFn` we treat the state as being able to read
                // more from the unbuffered stream. If we set them to 0
                // and 0, the code would think we already hit EOF.
                .start_index = buffer_size,
                .end_index = buffer_size,

                .stream = InStream {
                    .readFn = readFn,
                },
            };
        }

        fn readFn(in_stream: &InStream, dest: []u8) -> %usize {
            const self = @fieldParentPtr(Self, "stream", in_stream);

            var dest_index: usize = 0;
            while (true) {
                const dest_space = dest.len - dest_index;
                if (dest_space == 0) {
                    return dest_index;
                }
                const amt_buffered = self.end_index - self.start_index;
                if (amt_buffered == 0) {
                    assert(self.end_index <= buffer_size);
                    if (self.end_index == buffer_size) {
                        // we can read more data from the unbuffered stream
                        if (dest_space < buffer_size) {
                            self.start_index = 0;
                            self.end_index = %return self.unbuffered_in_stream.read(self.buffer[0..]);
                        } else {
                            // asking for so much data that buffering is actually less efficient.
                            // forward the request directly to the unbuffered stream
                            const amt_read = %return self.unbuffered_in_stream.read(dest[dest_index..]);
                            return dest_index + amt_read;
                        }
                    } else {
                        // reading from the unbuffered stream returned less than we asked for
                        // so we cannot read any more data.
                        return dest_index;
                    }
                }
                const copy_amount = math.min(dest_space, amt_buffered);
                const copy_end_index = self.start_index + copy_amount;
                mem.copy(u8, dest[dest_index..], self.buffer[self.start_index..copy_end_index]);
                self.start_index = copy_end_index;
                dest_index += copy_amount;
            }
        }
    };
}

pub const BufferedOutStream = BufferedOutStreamCustom(os.page_size);

pub fn BufferedOutStreamCustom(comptime buffer_size: usize) -> type {
    return struct {
        const Self = this;

        pub stream: OutStream,

        unbuffered_out_stream: &OutStream,

        buffer: [buffer_size]u8,
        index: usize,

        pub fn init(unbuffered_out_stream: &OutStream) -> Self {
            return Self {
                .unbuffered_out_stream = unbuffered_out_stream,
                .buffer = undefined,
                .index = 0,
                .stream = OutStream {
                    .writeFn = writeFn,
                },
            };
        }

        pub fn flush(self: &Self) -> %void {
            if (self.index == 0)
                return;

            %return self.unbuffered_out_stream.write(self.buffer[0..self.index]);
            self.index = 0;
        }

        fn writeFn(out_stream: &OutStream, bytes: []const u8) -> %void {
            const self = @fieldParentPtr(Self, "stream", out_stream);

            if (bytes.len >= self.buffer.len) {
                %return self.flush();
                return self.unbuffered_out_stream.write(bytes);
            }
            var src_index: usize = 0;

            while (src_index < bytes.len) {
                const dest_space_left = self.buffer.len - self.index;
                const copy_amt = math.min(dest_space_left, bytes.len - src_index);
                mem.copy(u8, self.buffer[self.index..], bytes[src_index..src_index + copy_amt]);
                self.index += copy_amt;
                assert(self.index <= self.buffer.len);
                if (self.index == self.buffer.len) {
                    %return self.flush();
                }
                src_index += copy_amt;
            }
        }
    };
}
