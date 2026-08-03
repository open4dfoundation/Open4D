namespace TVMEditor.Structures
{
    public class Face
    {
        public int V1 { get; set; }
        public int V2 { get; set; }
        public int V3 { get; set; }

        public Face() { }

        public Face(int v1, int v2, int v3)
        {
            V1 = v1;
            V2 = v2;
            V3 = v3;
        }

        public Face Opposite()
        {
            return new Face
            {
                V1 = V3,
                V2 = V2,
                V3 = V1
            };
        }

        public override bool Equals(object obj)
        {
            if (!(obj is Face face))
                return false;

            return V1 == face.V1 && V2 == face.V2 && face.V3 == V3;
        }

        public override int GetHashCode()
        {
            return 1000000 * V1 + 1000 * V2 + V3;
        }

        public Edge[] Edges => new Edge[]
        {
        new Edge(V1, V2),
        new Edge(V2, V3),
        new Edge(V3, V1)
        };
    }

}
