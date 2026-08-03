using System.Numerics;

namespace TVMEditor.Editing.AffinityCalculation
{
    public interface IAffinityCalculation
    {
        /// <summary>
        /// Computes affinity matrix
        /// </summary>
        /// <param name="volumeCenters">Tracked centers</param>
        /// <returns>Affinity matrix</returns>
        float[,] CalculateCentersAffinity(Vector3[][] volumeCenters);

        /// <summary>
        /// Returns computed affinity
        /// </summary>
        /// <returns>Affinity matrix</returns>
        float[,] GetCentersAffinity();

        void SetCentersAffinity(float[,] affinity);
    }
}
