using System;
using System.Linq;

namespace TVMEditor.Structures
{
    public class TriangleMeshSequence
    {
        public TriangleMesh[] Meshes { get; set; }

        public TriangleMeshSequence Clone()
        {
            return new TriangleMeshSequence
            {
                Meshes = Meshes.Select(m => m.Clone()).ToArray()
            };
        }
    }
}
