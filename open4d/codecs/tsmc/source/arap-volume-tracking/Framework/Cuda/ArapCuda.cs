//
// P/Invoke bindings for the native accelerator in Native/ (libarapcuda.so).
//

using System;
using System.Numerics;
using System.Runtime.InteropServices;

namespace Framework
{
    public enum Backend
    {
        /// <summary>Original managed implementation</summary>
        Cpu,
        /// <summary>Native implementation of the CUDA algorithm, run on the CPU (for validation without a GPU)</summary>
        NativeHost,
        /// <summary>CUDA kernels</summary>
        Cuda
    }

    public static class ArapCuda
    {
        const string Lib = "arapcuda";

        [DllImport(Lib)]
        static extern int arap_voxelize(Vector4[] points, int nPoints, Triangle[] tris, int nTris,
                                        float minX, float minY, float minZ, float a,
                                        int XRes, int YRes, int ZRes,
                                        byte[] output, nuint outBytes, int backend);

        [DllImport(Lib)]
        static extern NativeGrid arap_grid_create(byte[] bits, nuint nBytes, int XRes, int YRes, int ZRes,
                                                  float minX, float minY, float minZ, float a);

        [DllImport(Lib)]
        static extern void arap_grid_destroy(IntPtr grid);

        [DllImport(Lib)]
        static extern int arap_grid_assign(NativeGrid grid, Vector4[] points, int nPoints,
                                           int[] counts, double[] sums, double[] second);

        [DllImport(Lib)]
        static extern int arap_cuda_device_count();

        [DllImport(Lib)]
        static extern IntPtr arap_last_error();

        public static int DeviceCount() => arap_cuda_device_count();

        public static void Voxelize(TriangleMesh mesh, Vector4 min, float a, int XRes, int YRes, int ZRes, byte[] output, Backend backend)
        {
            if (backend == Backend.Cpu)
                throw new ArgumentException("Voxelize requires a native backend", nameof(backend));

            int rc = arap_voxelize(mesh.Points, mesh.Points.Length, mesh.Triangles, mesh.Triangles.Length,
                                   min.X, min.Y, min.Z, a, XRes, YRes, ZRes,
                                   output, (nuint)output.Length, backend == Backend.Cuda ? 1 : 0);
            Check(rc);
        }

        /// <summary>
        /// Occupancy grid uploaded to the GPU; freed when disposed or finalized.
        /// </summary>
        public sealed class NativeGrid : SafeHandle
        {
            public NativeGrid() : base(IntPtr.Zero, true) { }

            public override bool IsInvalid => handle == IntPtr.Zero;

            protected override bool ReleaseHandle()
            {
                arap_grid_destroy(handle);
                return true;
            }
        }

        /// <summary>
        /// Per-point accumulation over the cells nearest to each point.
        /// Sums[3i..3i+2] = sum of cell centers, Second[6i..6i+5] = sum of xx, xy, xz, yy, yz, zz.
        /// </summary>
        public sealed class CellMoments
        {
            public int[] Counts;
            public double[] Sums;
            public double[] Second;
        }

        public static NativeGrid CreateGrid(byte[] bits, int XRes, int YRes, int ZRes, Vector4 min, float a)
        {
            NativeGrid grid = arap_grid_create(bits, (nuint)bits.Length, XRes, YRes, ZRes, min.X, min.Y, min.Z, a);
            if (grid.IsInvalid)
                throw new Exception($"arapcuda: {Marshal.PtrToStringAnsi(arap_last_error())}");
            return grid;
        }

        public static CellMoments Assign(NativeGrid grid, Vector4[] points, bool secondMoments)
        {
            var m = new CellMoments
            {
                Counts = new int[points.Length],
                Sums = new double[3 * points.Length],
                Second = secondMoments ? new double[6 * points.Length] : null
            };
            Check(arap_grid_assign(grid, points, points.Length, m.Counts, m.Sums, m.Second));
            return m;
        }

        static void Check(int rc)
        {
            if (rc != 0)
                throw new Exception($"arapcuda error {rc}: {Marshal.PtrToStringAnsi(arap_last_error())}");
        }
    }
}
