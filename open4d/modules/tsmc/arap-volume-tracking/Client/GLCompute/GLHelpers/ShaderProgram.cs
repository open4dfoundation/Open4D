//
// Copyright (c) 2022,2023 Jan Dvořák, Zuzana Káčereková, Petr Vaněček, Lukáš Hruda, Libor Váša
// Licensed under the MIT License
//

using OpenTK.Graphics.OpenGL4;
using System;
using System.Collections.Generic;

namespace Client.GLCompute
{
    /// <summary>
    /// Reprezentuje kompletni shader program, ktery se sklada z jednotlivych shaderu
    /// </summary>
    public class ShaderProgram
    {
        /// <summary>
        /// ID gl objektu
        /// </summary>
        public int ID;

        /// <summary>
        /// Status slinkovani
        /// </summary>
        public bool linked;

        /// <summary>
        /// Log slinkovani
        /// </summary>
        public string log;

        public Dictionary<string, UniformInfo> uniforms;
        public Dictionary<string, AttribInfo> attribs;

        public ShaderProgram(params Shader[] shaders)
        {
            ID = GL.CreateProgram();
            foreach (var shader in shaders)
            {
                GL.AttachShader(ID, shader.ID);
                log += shader.type + shader.log;
            }
            GL.LinkProgram(ID);

            GL.GetProgram(ID, GetProgramParameterName.LinkStatus, out var code);
            var infolog = GL.GetProgramInfoLog(ID);
            linked = (code != (int)OpenTK.Graphics.OpenGL.Boolean.False);
            log += GL.GetProgramInfoLog(ID);

            if (!linked)
                Console.WriteLine(log);

            foreach (var shader in shaders)
            {
                GL.DetachShader(ID, shader.ID);
            }

            uniforms = new Dictionary<string, UniformInfo>();

            if (linked)
            {
                GL.GetProgram(ID, GetProgramParameterName.ActiveUniforms, out var count);
                for (int i = 0; i < count; i++)
                {
                    string name = GL.GetActiveUniform(ID, i, out var size, out var type);
                    int location = GL.GetUniformLocation(ID, name);
                    uniforms.Add(name, new UniformInfo() { Name = name, Location = location, Size = size, Type = type });
                }

                attribs = new Dictionary<string, AttribInfo>();
                GL.GetProgram(ID, GetProgramParameterName.ActiveAttributes, out count);
                for (int i = 0; i < count; i++)
                {
                    string name = GL.GetActiveAttrib(ID, i, out var size, out var type);
                    int location = GL.GetAttribLocation(ID, name);
                    attribs.Add(name, new AttribInfo() { Name = name, Location = location, Size = size, Type = type });
                }
            }
        }

        public void RegisterUniform(string key)
        {
            if (!uniforms.ContainsKey(key))
            {
                uniforms.Add(key, new UniformInfo() { Location = -1 });
            }
        }


        public void Delete()
        {
            GL.GetProgram(ID, GetProgramParameterName.DeleteStatus, out var code);
            if (code == (int)All.False)
                GL.DeleteProgram(ID);
        }

    }
}
