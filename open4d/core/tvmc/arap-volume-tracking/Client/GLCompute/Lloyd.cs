//
// Copyright (c) 2022,2023 Jan Dvořák, Zuzana Káčereková, Petr Vaněček, Lukáš Hruda, Libor Váša
// Licensed under the MIT License
//

using Framework;
using OpenTK.Graphics.OpenGL4;
using System;
using System.Collections.Generic;
using static Client.GLCompute.GLHelpers.ResourceLoader;

namespace Client.GLCompute
{
    class Lloyd
    {
        protected int CELLS;
        protected int CENTERS;
        static Lloyd _instance;

        // TODO: add to config file
        protected int ITERATIONS = 100;
        readonly int DIVISOR_SHIFT = 3;
        readonly int APPROX_CENTER_DIVISIOR = 4;

        public static Lloyd Instance(Config config)
        {
            if (_instance == null)
            {
                Console.WriteLine("Creating lloyd instance");
                _instance = new();
                _instance.CELLS = config.volumeGridResolution;
                _instance.CENTERS = config.pointCount;
                ComputeWindow.Instance().Invoke(_instance.Init);
            }
            return _instance;
        }


        int gridsize;
        int cellsize;
        int centersInput;

        float[] cellsLists;
        int cellsListsVBO;
        uint approxCentersCount;

        int iteration = 0;

        ShaderProgram step;
        ShaderProgram prestep;

        protected uint[] activeIndices;
        protected int activeIndicesVBO;
        protected uint[][] centers;
        protected int[] centersVBO;

        void Init()
        {
            Profiler.Profile("Lloyd Init", () => {
                gridsize = CELLS >> DIVISOR_SHIFT;
                cellsize = 1 << DIVISOR_SHIFT;

                approxCentersCount = (uint)(CENTERS / APPROX_CENTER_DIVISIOR);

                cellsLists = new float[gridsize * gridsize * gridsize * approxCentersCount * 4];

                cellsListsVBO = GL.GenBuffer();
                GL.BindBuffer(BufferTarget.ArrayBuffer, cellsListsVBO);
                GL.ObjectLabel(ObjectLabelIdentifier.Buffer, cellsListsVBO, -1, "Cells List");
                GL.BufferData(BufferTarget.ArrayBuffer, cellsLists.Length * 4, IntPtr.Zero, BufferUsageHint.DynamicCopy);
                GL.BindBuffer(BufferTarget.ArrayBuffer, 0);

                step = new ShaderProgram(new Shader(OpenTK.Graphics.OpenGL4.ShaderType.ComputeShader, Load("Client.GLCompute.Lloyd.step.comp")));
                prestep = new ShaderProgram(new Shader(OpenTK.Graphics.OpenGL4.ShaderType.ComputeShader, Load("Client.GLCompute.Lloyd.preproces.comp")));
            });
        }

        public void Run(in uint[] activeIndices, in uint[][] centers, in int activeIndicesVBO, in int[] centersVBO, out uint[] lloydCenters)
        {
            this.activeIndices = activeIndices;
            this.activeIndicesVBO = activeIndicesVBO;
            this.centers = centers;
            this.centersVBO = centersVBO;
            ComputeWindow.Instance().Invoke(Initialization);

            for (int i = 0; i < ITERATIONS; i++)
            {
                ComputeWindow.Instance().InvokeAsync(Step);
            }

            ComputeWindow.Instance().Invoke(GatherData);

            lloydCenters = this.centers[ITERATIONS % 2];
        }


        void Initialization()
        {
            centersInput = 0;
            GenerateActiveGridCells();

            GL.UseProgram(step.ID);
            GL.Uniform1(step.uniforms["dataLength"].Location, activeIndices.Length / 4);
            GL.Uniform3(step.uniforms["gridSize"].Location, gridsize, gridsize, gridsize);
            GL.Uniform1(step.uniforms["maxgridcenters"].Location, (int)approxCentersCount);
            GL.Uniform1(step.uniforms["bitshift"].Location, DIVISOR_SHIFT);
            GL.BindBufferBase(BufferRangeTarget.ShaderStorageBuffer, 0, activeIndicesVBO);
        }

        void Step()
        {
            iteration++;
            StepPreProcess(centersInput);
            Process(centersInput);
            centersInput = (centersInput + 1) % 2;
        }

        void Process(int centersInput)
        {
            GL.UseProgram(step.ID);
            GL.Uniform1(step.uniforms["iteration"].Location, iteration);
            GL.BindBufferBase(BufferRangeTarget.ShaderStorageBuffer, 0, activeIndicesVBO);
            GL.BindBufferBase(BufferRangeTarget.ShaderStorageBuffer, 1, centersVBO[centersInput]);
            GL.BindBufferBase(BufferRangeTarget.ShaderStorageBuffer, 2, centersVBO[(centersInput + 1) % 2]);
            GL.BindBufferBase(BufferRangeTarget.ShaderStorageBuffer, 3, cellsListsVBO);
            //GL.DebugMessageInsert(DebugSourceExternal.DebugSourceApplication, DebugType.DebugTypeMarker, 0, DebugSeverity.DebugSeverityNotification, -1, "StepProcess Dispatch");
            GL.MemoryBarrier(MemoryBarrierFlags.ShaderStorageBarrierBit);
            GL.DispatchCompute(activeIndices.Length / 64 + 1, 1, 1);

            GL.MemoryBarrier(MemoryBarrierFlags.ShaderStorageBarrierBit);
        }

        void StepPreProcess(int centersInput)
        {
            GL.UseProgram(prestep.ID);

            GL.Uniform1(prestep.uniforms["dataLength"].Location, activeGridCells.Length / 4);
            GL.Uniform1(prestep.uniforms["centersCnt"].Location, CENTERS);
            GL.Uniform1(prestep.uniforms["approxCentersCount"].Location, (int)approxCentersCount);
            GL.Uniform1(prestep.uniforms["iteration"].Location, iteration);
            GL.Uniform1(prestep.uniforms["gridsize"].Location, gridsize);
            GL.Uniform1(prestep.uniforms["cellsize"].Location, cellsize);
            GL.Uniform1(prestep.uniforms["diag"].Location, 3 * cellsize * cellsize);

            GL.BindBufferBase(BufferRangeTarget.ShaderStorageBuffer, 0, activeGridCellsVBO);
            GL.BindBufferBase(BufferRangeTarget.ShaderStorageBuffer, 1, centersVBO[centersInput]);
            GL.BindBufferBase(BufferRangeTarget.ShaderStorageBuffer, 3, cellsListsVBO);
            GL.DispatchCompute(activeGridCells.Length / (4 * 64) + 1, 1, 1);

            GL.MemoryBarrier(MemoryBarrierFlags.ShaderStorageBarrierBit);
        }


        void GatherData()
        {
            GL.GetNamedBufferSubData<uint>(centersVBO[ITERATIONS % 2], IntPtr.Zero, centers[ITERATIONS % 2].Length * 4, centers[ITERATIONS % 2]);
        }


        int[] activeGridCells;
        int activeGridCellsVBO;
        int activeGridCellsVBOCurrentLength = 0;
        void GenerateActiveGridCells()
        {
            int index = 0;
            HashSet<int> cellHash = new();
            List<int> cells = new();
            for (int i = 0; i < activeIndices.Length / 4; i++)
            {
                int x = (int)(activeIndices[index++]) >> DIVISOR_SHIFT;
                int y = (int)(activeIndices[index++]) >> DIVISOR_SHIFT;
                int z = (int)(activeIndices[index++]) >> DIVISOR_SHIFT;
                index++;

                int gridindex = ((z * gridsize + y) * gridsize + x);
                if (cellHash.Contains(gridindex)) continue;
                cellHash.Add(gridindex);
                cells.Add(x);
                cells.Add(y);
                cells.Add(z);
                cells.Add(gridindex);
            }
            activeGridCells = cells.ToArray();
            if (activeGridCellsVBOCurrentLength < activeGridCells.Length)
            {
                GL.DeleteBuffer(activeGridCellsVBO);
                activeGridCellsVBO = GL.GenBuffer();
                GL.BindBuffer(BufferTarget.ArrayBuffer, activeGridCellsVBO);
                GL.ObjectLabel(ObjectLabelIdentifier.Buffer, activeGridCellsVBO, -1, "Active Grid Cells");
                activeGridCellsVBOCurrentLength = (int)(activeGridCells.Length * 1.2);
                GL.BindBuffer(BufferTarget.ArrayBuffer, activeGridCellsVBO);
                GL.BufferData(BufferTarget.ArrayBuffer, activeGridCellsVBOCurrentLength * 4, IntPtr.Zero, BufferUsageHint.DynamicDraw);
                GL.DebugMessageInsert(DebugSourceExternal.DebugSourceApplication, DebugType.DebugTypePerformance, 0, DebugSeverity.DebugSeverityHigh, -1, "Active Grid Cells resized");
            }
            GL.BindBuffer(BufferTarget.ArrayBuffer, activeGridCellsVBO);
            GL.BufferSubData(BufferTarget.ArrayBuffer, IntPtr.Zero, activeGridCells.Length * 4, activeGridCells);
        }

    }
}

