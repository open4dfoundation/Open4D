using System.Numerics;
using TVMEditor.Structures;

namespace TVMEditor.Editing.SurfaceDeformation
{
    public interface ISurfaceDeformation
    {
        /// <summary>
        /// Deforms surface vertices
        /// </summary>
        /// <param name="vertices">Mesh vertices</param>
        /// <param name="oldCenters">Old centers positions</param>
        /// <param name="newCenters">New centers positions</param>
        /// <param name="frameIndex">Index of edited frame</param>
        /// <returns>Deformed mesh vertices</returns>
        TriangleMesh DeformSurface(Vector3[] vertices, Face[] faces, Vector3[] oldCenters, Vector3[] newCenters, int frameIndex, DualQuaternion[] transformations);

        DualQuaternion[] ComputeDeformations(Vector3[] vertices, Face[] faces, Vector3[] oldCenters, Vector3[] newCenters, int frameIndex, DualQuaternion[] transformations);
    }
}
