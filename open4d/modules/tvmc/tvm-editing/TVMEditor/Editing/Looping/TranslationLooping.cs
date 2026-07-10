using System.Numerics;
using TVMEditor.Structures;

namespace TVMEditor.Editing.Looping
{
    public class TranslationLooping : ILooping
    {
        public DualQuaternion[] ComputeFirstFrameCentersTransformations(Vector3[][] centers)
        {
            var centersCount = centers[0].Length;
            var framesCount = centers.Length;

            // Compute average centers positions
            var averagePositions = new Vector3[centersCount];
            for (var c = 0; c < centersCount; c++)
            {
                averagePositions[c] = 0.5f * (centers[0][c] + centers[framesCount - 1][c]);
            }

            var firstToAverage = new DualQuaternion[centersCount];

            for (var c = 0; c < centersCount; c++)
            {
                firstToAverage[c] = DualQuaternion.Translation(averagePositions[c] - centers[0][c]);
            }

            return firstToAverage;
        }

        public DualQuaternion[] ComputeLastFrameCentersTransformations(Vector3[][] centers)
        {
            var centersCount = centers[0].Length;
            var framesCount = centers.Length;

            // Compute average centers positions
            /*var averagePositions = new Vector3[centersCount];
            for (var c = 0; c < centersCount; c++)
            {
                averagePositions[c] = 0.5f * (centers[0][c] + centers[framesCount - 1][c]);
            }*/

            var lastToAverage = new DualQuaternion[centersCount];

            for (var c = 0; c < centersCount; c++)
            {
                lastToAverage[c] = DualQuaternion.Translation(centers[0][c] - centers[framesCount - 1][c]);
            }

            return lastToAverage;
        }
    }
}
