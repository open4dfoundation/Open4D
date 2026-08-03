//
// Copyright (c) 2022,2023 Jan Dvořák, Zuzana Káčereková, Petr Vaněček, Lukáš Hruda, Libor Váša
// Licensed under the MIT License
//

using OpenTK.Graphics.OpenGL4;
using System;

namespace Client.GLCompute
{
    public class VolumeSampling
    {
        static VolumeSampling _instance;

        int Centers;
        readonly uint[][] centers = new uint[2][];
        readonly int[] centersVBO = new int[2];

        uint[] activeCells;

        public static VolumeSampling Instance(Config config)
        {
            if (_instance == null)
            {
                Console.WriteLine("Creating volume sampler instance");
                _instance = new VolumeSampling();
                _instance.Centers = config.pointCount;
                ComputeWindow.Instance().Invoke(_instance.Init);
            }
            return _instance;
        }

        void Init()
        {
            centers[0] = new uint[Centers * 4];
            centers[1] = new uint[Centers * 4];
            GL.GenBuffers(2, centersVBO);
            GL.BindBuffer(BufferTarget.ArrayBuffer, centersVBO[0]);
            GL.ObjectLabel(ObjectLabelIdentifier.Buffer, centersVBO[0], -1, "Centers[0]");
            GL.BindBufferBase(BufferRangeTarget.ShaderStorageBuffer, 0, centersVBO[0]);
            GL.BufferData(BufferTarget.ShaderStorageBuffer, Centers * 4 * 4, IntPtr.Zero, BufferUsageHint.StreamRead);
            GL.BindBuffer(BufferTarget.ArrayBuffer, centersVBO[1]);
            GL.ObjectLabel(ObjectLabelIdentifier.Buffer, centersVBO[1], -1, "Centers[1]");
            GL.BindBufferBase(BufferRangeTarget.ShaderStorageBuffer, 0, centersVBO[1]);
            GL.BufferData(BufferTarget.ShaderStorageBuffer, Centers * 4 * 4, IntPtr.Zero, BufferUsageHint.StreamRead);
        }

        public void Run(in uint[] activeCells, out uint[][] centers, out int[] centersVBO)
        {
            this.activeCells = activeCells;
            ComputeWindow.Instance().Invoke(Run);
            centers = this.centers;
            centersVBO = this.centersVBO;
        }

        void Run()
        {
            Random r = new Random(0);
            int restIndices = activeCells.Length / 4;
            int restCenters = Centers;
            int currentIndex = -1;

            int step = activeCells.Length / Centers;
            int c;
            for (int i = 0; i < Centers; i++)
            {
                step = restIndices / restCenters;
                c = r.Next(1, 2 * step);
                currentIndex += c;
                currentIndex = Math.Min(currentIndex, activeCells.Length / 4 - restCenters);
                restIndices -= c;
                restCenters--;
                centers[0][4 * i] = activeCells[4 * currentIndex];
                centers[0][4 * i + 1] = activeCells[4 * currentIndex + 1];
                centers[0][4 * i + 2] = activeCells[4 * currentIndex + 2];
                centers[0][4 * i + 3] = 1;
            }

            GL.BindBuffer(BufferTarget.ArrayBuffer, centersVBO[0]);
            GL.BindBufferBase(BufferRangeTarget.ShaderStorageBuffer, 0, centersVBO[0]);
            GL.BufferSubData(BufferTarget.ShaderStorageBuffer, IntPtr.Zero, Centers * 4 * 4, centers[0]);
            GL.BindBuffer(BufferTarget.ArrayBuffer, centersVBO[1]);
            GL.BindBufferBase(BufferRangeTarget.ShaderStorageBuffer, 0, centersVBO[1]);
            GL.BufferSubData(BufferTarget.ShaderStorageBuffer, IntPtr.Zero, Centers * 4 * 4, centers[1]);
            GL.Flush();
            GL.MemoryBarrier(MemoryBarrierFlags.AllBarrierBits);
        }
    }
}
