using System.Buffers.Binary;
using System.Security.Cryptography;
using System.Text;

namespace U9GL.Core.Saves;

internal static class U9Bytes
{
    static U9Bytes()
    {
        Encoding.RegisterProvider(CodePagesEncodingProvider.Instance);
        Windows1252 = Encoding.GetEncoding(1252, EncoderFallback.ExceptionFallback, DecoderFallback.ExceptionFallback);
    }

    public static Encoding Windows1252 { get; }

    public static int I32(ReadOnlySpan<byte> data, long offset) =>
        BinaryPrimitives.ReadInt32LittleEndian(data.Slice(Checked(offset, 4, data.Length), 4));

    public static uint U32(ReadOnlySpan<byte> data, long offset) =>
        BinaryPrimitives.ReadUInt32LittleEndian(data.Slice(Checked(offset, 4, data.Length), 4));

    public static short I16(ReadOnlySpan<byte> data, long offset) =>
        BinaryPrimitives.ReadInt16LittleEndian(data.Slice(Checked(offset, 2, data.Length), 2));

    public static ushort U16(ReadOnlySpan<byte> data, long offset) =>
        BinaryPrimitives.ReadUInt16LittleEndian(data.Slice(Checked(offset, 2, data.Length), 2));

    public static float F32(ReadOnlySpan<byte> data, long offset) => BitConverter.Int32BitsToSingle(I32(data, offset));

    public static string Sha256(ReadOnlySpan<byte> data) => Convert.ToHexStringLower(SHA256.HashData(data));

    public static long FloorDiv(long a, long b) => (a / b) - ((a % b != 0 && (a < 0) != (b < 0)) ? 1 : 0);

    private static int Checked(long offset, int size, int length)
    {
        if (offset < 0 || offset + size > length)
            throw new IndexOutOfRangeException($"read of {size} bytes at {offset} is outside {length} bytes");
        return (int)offset;
    }
}
