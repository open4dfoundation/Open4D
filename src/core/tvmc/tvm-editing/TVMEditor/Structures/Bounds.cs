using System.Numerics;

namespace TVMEditor.Structures
{
    public struct Bounds
    {
        public Vector3 Center { get; set; }
        public Vector3 Size { get; set; }

        public Bounds(Vector3 center, Vector3 size)
        {
            Center = center;
            Size = size;
        }
    }
}
