//
// Copyright (c) 2022,2023 Jan Dvořák, Zuzana Káčereková, Petr Vaněček, Lukáš Hruda, Libor Váša
// Licensed under the MIT License
//

using OpenTK.Graphics.OpenGL4;


namespace Client.GLCompute
{
    public struct AttribInfo
    {
        public string Name { get; set; }
        public int Location { get; set; }
        public int Size { get; set; }
        public ActiveAttribType Type { get; set; }
    }
}
