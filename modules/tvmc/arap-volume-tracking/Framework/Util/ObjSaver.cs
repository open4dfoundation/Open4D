//
// Copyright (c) 2022,2023 Jan Dvořák, Zuzana Káčereková, Petr Vaněček, Lukáš Hruda, Libor Váša
// Licensed under the MIT License
//

using System.Globalization;
using System.IO;
using System.Numerics;

namespace Framework
{
    /// <summary>
    /// Author: Jan Dvořák
    /// </summary>
    public class ObjSaver
    {
        /// <summary>
        /// Author: Jan Dvořák
        /// </summary>
        public void Execute(TriangleMesh mesh, string filename)
        {
            StreamWriter writer = new StreamWriter(filename);

            NumberFormatInfo nfi = new NumberFormatInfo();
            nfi.NumberDecimalSeparator = ".";
            nfi.NumberGroupSeparator = "";


            writer.WriteLine("# Points");
            for (int i = 0; i < mesh.Points.Length; i++)
            {
                Vector4 point = mesh.Points[i];
                writer.WriteLine("v " + point.X.ToString(nfi) + " " + point.Y.ToString(nfi) + " " + (-point.Z).ToString(nfi));
            }

            writer.WriteLine("# Faces");
            for (int i = 0; i < mesh.Triangles.Length; i++)
            {
                Triangle t = mesh.Triangles[i];
                writer.WriteLine("f " + (t.V1 + 1) + " " + (t.V2 + 1) + " " + (t.V3 + 1));
            }

            writer.Close();
        }
    }
}
