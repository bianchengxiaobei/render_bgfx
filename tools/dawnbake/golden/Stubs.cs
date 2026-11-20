// Minimal stubs required to compile the frozen Dawn protocol sources without Unity.
using System;
using System.Runtime.InteropServices;

namespace ProtocolCmp
{
    [AttributeUsage(AttributeTargets.Struct | AttributeTargets.Class)]
    public sealed class MemoryLayoutAttribute : Attribute
    {
    }
}

namespace NHive
{
    [StructLayout(LayoutKind.Sequential)]
    public struct FGuid
    {
        public uint A;
        public uint B;
        public uint C;
        public uint D;
    }
}

namespace UnityEngine
{
    [StructLayout(LayoutKind.Sequential)]
    public struct Vector3Int
    {
        public int x;
        public int y;
        public int z;

        public Vector3Int(int x, int y, int z)
        {
            this.x = x;
            this.y = y;
            this.z = z;
        }
    }
}
