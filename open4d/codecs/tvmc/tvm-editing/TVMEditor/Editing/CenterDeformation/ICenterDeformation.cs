using System.Numerics;
using TVMEditor.Structures;

namespace TVMEditor.Editing.CenterDeformation
{
    /// <summary>
    /// Interface for propagation of center deformation in a single frame
    /// </summary>
    public interface ICenterDeformation
    {
        /// <summary>
        /// Deforms centers according to selected center movement
        /// </summary>
        /// <param name="centers">Centers positions</param>
        /// <param name="centerIndex">Selected center index</param>
        /// <param name="newPosition">New position of selected center</param>
        /// <returns>Transformed positions of all centers in a given frame</returns>
        Vector3[] DeformCenters(Vector3[] centers, int[] centerIndices, Vector3[] newPositions, ref DualQuaternion[] transformations);

        Vector3[] DeformCenters2(Vector3[] centers, int[] centerIndices, Vector3[] newPositions, ref DualQuaternion[] dualQuaternions);
    }
}
