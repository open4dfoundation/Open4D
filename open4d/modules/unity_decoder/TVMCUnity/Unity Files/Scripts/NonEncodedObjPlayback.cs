using UnityEngine;
using System.Linq;
using System.Diagnostics;

public class NewMonoBehaviourScript : MonoBehaviour
{
    [SerializeField] private string sequenceTag;
    [SerializeField] private int playbackFPS;
    private MeshRenderer[] sequenceRenderers;
    private int currentFrame;
    private float playbackTimer;
    private int totalFrames;
    // Start is called once before the first execution of Update after the MonoBehaviour is created
    void Start()
    {
        GameObject[] unsortedSequence = GameObject.FindGameObjectsWithTag(sequenceTag);
        GameObject[] sequence = unsortedSequence.OrderBy(go => go.name).ToArray();
        currentFrame = 0;
        totalFrames = unsortedSequence.Length;
        sequenceRenderers = new MeshRenderer[totalFrames];
        for (int i = 0; i < sequence.Length; i++)
        {
            MeshRenderer renderer = sequence[i].GetComponentInChildren<MeshRenderer>();
            if (renderer != null)
            {
                renderer.enabled = false;
                sequenceRenderers[i] = renderer;
            }
            else
            {
                UnityEngine.Debug.LogWarning("Object in sequence is invalid! Skipping...");
            }
        }
        if (sequenceRenderers[0] != null) sequenceRenderers[currentFrame].enabled = true;
    }

    // Update is called once per frame
    void Update()
    {
        playbackTimer += Time.deltaTime;
        if (playbackTimer >= 1f / playbackFPS)
        {
            playbackTimer -= 1f / playbackFPS;
            if (sequenceRenderers[currentFrame] != null) sequenceRenderers[currentFrame].enabled = false;
            currentFrame++;
            // Check if we need to advance subsequence
            if (currentFrame >= totalFrames)
            {
                currentFrame = 0;
            }
        }
        if (sequenceRenderers[currentFrame] != null) sequenceRenderers[currentFrame].enabled = true;
    }
}
