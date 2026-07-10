//
// Copyright (c) 2022,2023 Jan Dvořák, Zuzana Káčereková, Petr Vaněček, Lukáš Hruda, Libor Váša
// Licensed under the MIT License
//

using Framework;
using OpenTK.Graphics.OpenGL4;
using OpenTK.Mathematics;
using System;
using System.Collections.Generic;
using System.IO;
using static Client.GLCompute.GLHelpers.ResourceLoader;

namespace Client.GLCompute
{
    public class Voxelization
    {
        static Voxelization _instance;

        int cells;
        uint cellsx;
        uint cellsy;
        uint cellsz;
        uint[] volume;

        List<uint> cellList = new();
        public uint[] activeCells;


        ShaderProgram drawPass;
        FrameBuffer frameBuffer;

        ShaderProgram clearPass;
        ShaderProgram computePass;

        TriangleMesh mesh;

        System.Numerics.Vector4 size;
        System.Numerics.Vector4 center;
        float maxSize;

        int ssbo;
        int vao;
        int vb;
        int ib;

        public static Voxelization Instance(Config config)
        {
            if (_instance == null)
            {
                Console.WriteLine("Creating voxelization instance");
                _instance = new Voxelization();
                _instance.cells = config.volumeGridResolution;
                _instance.volume = new uint[_instance.cells * _instance.cells * _instance.cells];
                ComputeWindow.Instance().Invoke(_instance.Init);
            }
            return _instance;
        }

        //static uint RoundUp(uint number, uint multiple)
        //{
        //    return (number & ~(multiple - 1)) + multiple;
        //}


        void Init()
        {
            drawPass = new ShaderProgram(new VertexShader(Load("Client.GLCompute.Voxelization.vert")), new FragmentShader(Load("Client.GLCompute.Voxelization.frag")));
            GL.UseProgram(drawPass.ID);
            GL.Uniform3(drawPass.uniforms["volumeDim"].Location, cells, cells, cells);
            GL.UseProgram(0);
            GL.ClearColor(0, 0, 0, 0);
            frameBuffer = new FrameBuffer(cells, cells);
            clearPass = new ShaderProgram(new Shader(OpenTK.Graphics.OpenGL4.ShaderType.ComputeShader, Load("Client.GLCompute.Voxelization.clear.comp")));
            computePass = new ShaderProgram(new Shader(OpenTK.Graphics.OpenGL4.ShaderType.ComputeShader, Load("Client.GLCompute.Voxelization.comp")));

            ssbo = GL.GenBuffer();
            GL.BindBufferBase(BufferRangeTarget.ShaderStorageBuffer, 0, ssbo);
            GL.ObjectLabel(ObjectLabelIdentifier.Buffer, ssbo, -1, "Volume Buffer");
            GL.BufferData(BufferTarget.ShaderStorageBuffer, 4 * cells * cells * cells, IntPtr.Zero, BufferUsageHint.DynamicRead);
        }

        public void Run(in Framework.TriangleMesh mesh, out uint[] activeCells, out int activeCellsVBO)
        {
            this.mesh = mesh;
            ComputeWindow.Instance().Invoke(Run);
            activeCells = this.activeCells;
            activeCellsVBO = this.activeCellsVBO;
        }

        void Run()
        {
            var totalMem = GL.GetInteger((GetPName)0x9048);
            var freeMem = GL.GetInteger((GetPName)0x9049);
            GL.DebugMessageInsert(DebugSourceExternal.DebugSourceApplication, DebugType.DebugTypePerformance, 0, DebugSeverity.DebugSeverityNotification, -1, $"Free GPU memory: {freeMem / 1024} out of {totalMem / 1024}MB");

            activeCells = null;
            GC.Collect();
            size = mesh.BoundingBox.MaxPoint - mesh.BoundingBox.MinPoint;
            center = mesh.BoundingBox.MidPoint;
            maxSize = Math.Max(size.X, Math.Max(size.Y, size.Z));
            /*var ortho = Matrix4.CreateOrthographicOffCenter(
                center.X - maxSize / 2, 
                center.X + maxSize / 2, 
                center.Y - maxSize / 2, 
                center.Y + maxSize / 2, 
                -center.Z - maxSize / 2, 
                -center.Z + maxSize / 2
            );*/
            var ortho = Matrix4.CreateOrthographicOffCenter(
                center.X - size.X / 2,
                center.X - size.X / 2 + maxSize,
                center.Y - size.Y / 2,
                center.Y - size.Y / 2 + maxSize,
                -center.Z - size.Z / 2,
                -center.Z - size.Z / 2 + maxSize
            );

            GL.BindBufferBase(BufferRangeTarget.ShaderStorageBuffer, 0, ssbo);
            //GL.BufferData(BufferTarget.ShaderStorageBuffer, 4 * cells * cells * cells, IntPtr.Zero, BufferUsageHint.DynamicRead);

            GL.UseProgram(clearPass.ID);
            GL.Uniform3(computePass.uniforms["volumeDim"].Location, cells, cells, cells);
            GL.DispatchCompute(cells / 32, cells / 32, 1);

            vao = GL.GenVertexArray();
            vb = GL.GenBuffer();
            ib = GL.GenBuffer();

            GL.BindVertexArray(vao);

            GL.BindBuffer(BufferTarget.ArrayBuffer, vb);
            GL.BufferData(BufferTarget.ArrayBuffer, mesh.Points.Length * 4 * sizeof(float), mesh.Points, BufferUsageHint.StaticDraw);

            //GL.Enable(EnableCap.CullFace);
            GL.FrontFace(FrontFaceDirection.Cw);

            GL.EnableVertexAttribArray(0);
            GL.VertexAttribPointer(0, 4, VertexAttribPointerType.Float, false, 0, IntPtr.Zero);

            GL.BindBuffer(BufferTarget.ElementArrayBuffer, ib);
            GL.BufferData(BufferTarget.ElementArrayBuffer, mesh.Triangles.Length * 3 * sizeof(int), mesh.Triangles, BufferUsageHint.StaticDraw);

            //GL.BindFramebuffer(FramebufferTarget.Framebuffer, frameBuffer.ID);
            GL.BindBufferBase(BufferRangeTarget.ShaderStorageBuffer, 0, ssbo);
            GL.Viewport(0, 0, frameBuffer.width, frameBuffer.height);

            GL.Clear(ClearBufferMask.DepthBufferBit | ClearBufferMask.ColorBufferBit);

            GL.UseProgram(drawPass.ID);
            GL.UniformMatrix4(drawPass.uniforms["ortho"].Location, false, ref ortho);

            GL.DrawElements(PrimitiveType.Triangles, mesh.Triangles.Length * 3, DrawElementsType.UnsignedInt, IntPtr.Zero);

            //GL.BindFramebuffer(FramebufferTarget.Framebuffer, 0);
            ComputeWindow.Instance().SwapBuffers();
            GL.UseProgram(0);

            GL.BindVertexArray(0);
            GL.BindBuffer(BufferTarget.ArrayBuffer, 0);
            GL.BindBuffer(BufferTarget.ElementArrayBuffer, 0);
            GL.DeleteBuffer(vb);
            GL.DeleteBuffer(ib);
            GL.DeleteVertexArray(vao);

            GL.UseProgram(computePass.ID);
            GL.Uniform3(computePass.uniforms["volumeDim"].Location, cells, cells, cells);
            GL.DispatchCompute(cells / 32, cells / 32, 1);
            GL.Flush();

            GL.GetNamedBufferSubData<uint>(ssbo, IntPtr.Zero, cells * cells * cells * 4, volume);
            GL.MemoryBarrier(MemoryBarrierFlags.AllBarrierBits);
            GL.Flush();

            cellList.Clear();
            cellsx = 0;
            cellsy = 0;
            cellsz = 0;
            for (uint i = 0; i < volume.Length; i++)
            {
                if (volume[i] == 0)
                {
                    uint j = i;
                    uint iz = (uint)(j % cells);
                    j = (uint)(j / cells);
                    uint iy = (uint)(j % cells);
                    j = (uint)(j / cells);
                    uint ix = (uint)(j % cells);
                    cellList.Add(ix);
                    cellList.Add(iy);
                    cellList.Add(iz);
                    cellList.Add(0);
                    if (ix > cellsx) cellsx = ix;
                    if (iy > cellsy) cellsy = iy;
                    if (iz > cellsz) cellsz = iz;
                }
            }
            activeCells = cellList.ToArray();
            cellsx++;
            cellsy++;
            cellsz++;

            CreateActiveCellsBuffer();
        }

        int activeCellsVBO;
        int activeCellsCurrentLength = 0;
        void CreateActiveCellsBuffer()
        {
            if (activeCellsCurrentLength < activeCells.Length)
            {
                GL.DeleteBuffer(activeCellsVBO);
                activeCellsVBO = GL.GenBuffer();
                GL.BindBuffer(BufferTarget.ArrayBuffer, activeCellsVBO);
                activeCellsCurrentLength = (int)(activeCells.Length * 1.2);
                GL.BufferData(BufferTarget.ArrayBuffer, activeCellsCurrentLength * 4, IntPtr.Zero, BufferUsageHint.DynamicCopy);
                GL.DebugMessageInsert(DebugSourceExternal.DebugSourceApplication, DebugType.DebugTypePerformance, 0, DebugSeverity.DebugSeverityHigh, -1, "Active Cells resized");
            }
            GL.BindBuffer(BufferTarget.ArrayBuffer, activeCellsVBO);
            GL.BufferSubData(BufferTarget.ArrayBuffer, IntPtr.Zero, activeCells.Length * 4, activeCells);
            GL.BindBuffer(BufferTarget.ArrayBuffer, 0);
        }

        public System.Numerics.Vector4[] IjklToVector4(uint[] points)
        {
            var cellSize = maxSize / cells;
            System.Numerics.Vector4[] result = new System.Numerics.Vector4[points.Length / 4];
            for (int p = 0; p < result.Length; p++)
            {
                System.Numerics.Vector4 point = new(
                    center.X - size.X / 2 + cellSize * points[p * 4] / points[p * 4 + 3],
                    center.Y - size.Y / 2 + cellSize * points[p * 4 + 1] / points[p * 4 + 3],
                    center.Z - size.Z / 2 + cellSize * points[p * 4 + 2] / points[p * 4 + 3],
                0);
                result[p] = point;
            }

            return result;
        }

        public VolumeGrid SaveAsVG(string fn, bool toFile)
        {
            FileStream fs = null;
            BinaryWriter bw = null;

            if (toFile)
            {
                fs = new FileStream(fn, FileMode.Create);
                bw = new BinaryWriter(fs);
                bw.Write(cellsx);
                bw.Write(cellsy);
                bw.Write(cellsz);
                bw.Write(center.X - size.X / 2);
                bw.Write(center.Y - size.Y / 2);
                bw.Write(center.Z - size.Z / 2);
                bw.Write(maxSize / cells);
            }

            byte current = 0;
            int count = 0;

#if OUTPUT_VOXEL_XYZ
            using var xyz = new StreamWriter(fn + ".xyz");
#endif


            byte[][][] data;
            data = new byte[cellsx][][];
            for (int x = 0; x < cellsx; x++)
            {
#if OUTPUT_VOXEL_PBM
                using var pbm = new StreamWriter(fn + $".{x:000}.pbm");
                pbm.WriteLine($"P1\n{cellsz} {cellsy}");
#endif
                data[x] = new byte[cellsy][];
                for (int y = 0; y < cellsy; y++)
                {
                    data[x][y] = new byte[cellsz];
                    for (int z = 0; z < cellsz; z++)
                    {
                        long index = (x * cells + y) * cells + z;
                        if (volume[index] == 0)
                        {
                            data[x][y][z] = 255;

#if OUTPUT_VOXEL_XYZ
                            xyz.WriteLine($"{x} {y} {z}");
#endif
                        }

#if OUTPUT_VOXEL_PBM
                        pbm.Write(volume[index] == 0 ? "1 " : "0 ");
#endif

                        if (toFile)
                        {
                            var val = data[x][y][z];
                            if (val != current)
                            {
                                bw.Write(current);
                                bw.Write(count);
                                current = val;
                                count = 0;
                            }
                            count++;
                        }
                    }
#if OUTPUT_VOXEL_PBM
                    pbm.WriteLine();
#endif

                }
            }

            if (toFile)
            {
                bw.Write(current);
                bw.Write(count);

                bw.Close();
                fs.Close();
            }
            //return null;
            return new MemoryEfficientVolumeGrid(new(center.X - size.X / 2, center.Y - size.Y / 2, center.Z - size.Z / 2, 0), maxSize / cells, data, (int)cellsx, (int)cellsy, (int)cellsz);
        }
    }
}
