//
// Copyright (c) 2022,2023 Jan Dvořák, Zuzana Káčereková, Petr Vaněček, Lukáš Hruda, Libor Váša
// Licensed under the MIT License
//

using OpenTK.Graphics.OpenGL4;

namespace Client.GLCompute
{
    public class Shader
    {
        /// <summary>
        /// Id shader objektu
        /// </summary>
        public int ID;

        /// <summary>
        /// Stav prekladu
        /// </summary>
        public bool compiled;

        /// <summary>
        /// Log prekladu
        /// </summary>
        public string log;

        /// <summary>
        /// Typ shaderu
        /// </summary>
        public ShaderType type;

        public Shader(ShaderType type, string source)
        {
            this.type = type;
            ID = GL.CreateShader(type);
            GL.ShaderSource(ID, source);

            GL.CompileShader(ID);

            GL.GetShader(ID, ShaderParameter.CompileStatus, out var code);
            compiled = (code != (int)OpenTK.Graphics.OpenGL.Boolean.False);
            log = source + "\n" + GL.GetShaderInfoLog(ID);
        }

        /// <summary>
        /// Smaze shader (nastavi ke smazani)
        /// </summary>
        public void Delete()
        {
            GL.GetShader(ID, ShaderParameter.DeleteStatus, out var code);
            if (code == (int)All.False)
                GL.DeleteShader(ID);
        }
    }

    public class VertexShader : Shader
    {
        public VertexShader(string source) : base(ShaderType.VertexShader, source) { }
    }

    public class FragmentShader : Shader
    {
        public FragmentShader(string source) : base(ShaderType.FragmentShader, source) { }
    }

}
