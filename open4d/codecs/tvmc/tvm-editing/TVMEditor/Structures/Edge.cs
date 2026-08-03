namespace TVMEditor.Structures
{
    public struct Edge
    {
        public int V1 { get; set; }
        public int V2 { get; set; }

        public Edge(int v1, int v2)
        {
            V1 = v1;
            V2 = v2;
        }

        public override bool Equals(object obj)
        {
            if (!(obj is Edge edge))
                return false;

            return V1 == edge.V1 && V2 == edge.V2;
        }

        public override int GetHashCode()
        {
            return 1000 * V1 + V2;
        }

        public Edge Opposite()
        {
            return new Edge(V2, V1);
        }

        public Edge Unoriented()
        {
            if (V2 > V1)
                return this;
            return Opposite();
        }
    }
}
