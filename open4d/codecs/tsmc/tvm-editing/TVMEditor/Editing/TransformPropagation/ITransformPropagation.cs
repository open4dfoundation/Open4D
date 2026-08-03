using System.Numerics;
using TVMEditor.Structures;

namespace TVMEditor.Editing.TransformPropagation
{
    public interface ITransformPropagation
    {
        /// <summary>
        /// Propagates centers transformation into other frames
        /// </summary>
        /// <param name="centers">All centers</param>
        /// <param name="frameIndex">Current frame index</param>
        /// <param name="oldCenters">Old centers position in current frame</param>
        /// <param name="newCenters">New centers position in current frame</param>
        /// <returns>New positions of all centers</returns>
        Vector3[][] PropagateTransform(Vector3[][] centers, int frameIndex, Vector3[] oldCenters, Vector3[] newCenters, DualQuaternion[] transformations, out DualQuaternion[][] propagatedTransformations);

        bool FrameIsAffected(int editedFrameIndex, int frameIndex);
    }
}
