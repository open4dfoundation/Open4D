using UnityEngine;
using System;
using System.IO;
using System.Collections;
using UnityEngine.Networking;
using System.Collections.Generic;

public class TestCall : MonoBehaviour
{
    [SerializeField] private string sequenceDirectory = ""; // Path to directory containing subsequences
    [SerializeField] private int preLoadWindow = 3;
    [SerializeField] private int decodeWindow = 3;
    [SerializeField] private Material sequenceMat;
    [SerializeField] private bool enableLogging = false;
    private bool playbackManagerReady = false;
    private MeshFilter meshFilter;
    private int totalFrames;
    private int vertexCount;
    private int[] triangleIndices;
    private int currentFrame = 0;
    private Vector3[] vertexArray;
    public float playbackFPS = 30f;
    private float playbackTimer = 0f;
    private string workingDir;
    private int currentSubsequenceIndex = 1;
    private int nextToLoad;
    private int nextToDecode;

    
    void Start()
    {
        StartCoroutine(InitializeSequences());
    }
IEnumerator InitializeSequences()
{
    string destPath = Path.Combine(Application.persistentDataPath, sequenceDirectory);
    Debug.Log($"[Unity] Looking for sequences at: {destPath}");

    // Check if already exists with valid content
    if (Directory.Exists(destPath) && Directory.GetDirectories(destPath).Length > 0)
    {
        Debug.Log($"[Unity] Sequences already exist at {destPath}, using existing files");
    }
    else
    {
        Debug.Log($"[Unity] Setting up sequences...");
        bool success = false;
#if UNITY_ANDROID && !UNITY_EDITOR
        // Android/Quest implementation
        string sourcePath = Path.Combine(Application.streamingAssetsPath, $"{sequenceDirectory}.zip");
        
        Debug.Log($"[Unity] Loading zip from APK: {sourcePath}");
        
        using (UnityWebRequest www = UnityWebRequest.Get(sourcePath))
        {
            yield return www.SendWebRequest();
            
            if (www.result == UnityWebRequest.Result.Success)
            {
                Debug.Log($"[Unity] Zip loaded, extracting...");
                string tempZip = Path.Combine(Application.persistentDataPath, "temp.zip");
                File.WriteAllBytes(tempZip, www.downloadHandler.data);
                
                try
                {
                    System.IO.Compression.ZipFile.ExtractToDirectory(tempZip, Application.persistentDataPath);
                    File.Delete(tempZip);
                    
                    // Check if extraction created the expected folder
                    if (Directory.Exists(destPath))
                    {
                        success = true;
                        Debug.Log($"[Unity] Successfully extracted to {destPath}");
                    }
                    else
                    {
                        // Maybe the zip contains "Sequences" but we're looking for "DanceSequence"
                        string altPath = Path.Combine(Application.persistentDataPath, "Sequences");
                        if (Directory.Exists(altPath))
                        {
                            Directory.Move(altPath, destPath);
                            success = true;
                            Debug.Log($"[Unity] Moved Sequences to {destPath}");
                        }
                    }
                }
                catch (Exception e)
                {
                    Debug.LogError($"[Unity] Extraction failed: {e.Message}");
                }
            }
            else
            {
                Debug.LogError($"[Unity] Failed to load zip from APK: {www.error}");
            }
        }

#else
        string sourcePath = Path.Combine(Application.streamingAssetsPath, $"{sequenceDirectory}.zip");
        string tempExtractPath = Path.Combine(Application.persistentDataPath, "temp_extract_" + System.Guid.NewGuid().ToString());
        string tempZip = Path.Combine(Application.persistentDataPath, "temp.zip");

        try
        {
            // Clean up destination if it exists
            if (Directory.Exists(destPath))
            {
                Directory.Delete(destPath, true);
            }

            Debug.Log($"[Unity] Copying zip...");
            File.Copy(sourcePath, tempZip, true);
            
            // Extract to temp location
            Debug.Log($"[Unity] Extracting to temp location...");
            Directory.CreateDirectory(tempExtractPath);
            System.IO.Compression.ZipFile.ExtractToDirectory(tempZip, tempExtractPath);
            
            File.Delete(tempZip);
            
            // Debug: Show what was extracted
            Debug.Log($"[Unity] Extracted contents:");
            foreach (var item in Directory.GetFileSystemEntries(tempExtractPath))
            {
                Debug.Log($"  - {Path.GetFileName(item)} ({(Directory.Exists(item) ? "DIR" : "FILE")})");
            }
            
            // Find the actual sequences folder
            string extractedSequencePath = null;
            
            // Check if subsequence_001 exists directly in temp (sequences are loose)
            if (Directory.Exists(Path.Combine(tempExtractPath, "subsequence_001")))
            {
                Debug.Log("[Unity] Found loose subsequences in temp folder");
                extractedSequencePath = tempExtractPath;
            }
            // Check if there's a "Sequences" folder
            else if (Directory.Exists(Path.Combine(tempExtractPath, "Sequences")))
            {
                Debug.Log("[Unity] Found Sequences folder");
                extractedSequencePath = Path.Combine(tempExtractPath, "Sequences");
            }
            // Check if there's a folder matching sequenceDirectory  
            else if (Directory.Exists(Path.Combine(tempExtractPath, sequenceDirectory)))
            {
                Debug.Log($"[Unity] Found {sequenceDirectory} folder");
                extractedSequencePath = Path.Combine(tempExtractPath, sequenceDirectory);
            }
            // Just use the first non-Mac folder we find
            else
            {
                var dirs = Directory.GetDirectories(tempExtractPath);
                foreach (var dir in dirs)
                {
                    if (!dir.Contains("__MACOSX"))
                    {
                        Debug.Log($"[Unity] Using first valid folder: {Path.GetFileName(dir)}");
                        extractedSequencePath = dir;
                        break;
                    }
                }
            }
            
            if (extractedSequencePath != null && Directory.Exists(extractedSequencePath))
            {
                Debug.Log($"[Unity] Moving from {extractedSequencePath} to {destPath}");
                Directory.Move(extractedSequencePath, destPath);
                success = true;
            }
            else
            {
                Debug.LogError("[Unity] Could not find valid sequences in extraction");
            }
            
            // Clean up temp directory
            if (Directory.Exists(tempExtractPath))
            {
                Directory.Delete(tempExtractPath, true);
            }
        }
        catch (Exception e)
        {
            Debug.LogError($"[Unity] Failed: {e.Message}");
            success = false;
        }
#endif

          if (!success)
        {
            Debug.LogError("[Unity] Failed to set up sequences");
            yield break;
        }

        yield return new WaitForSeconds(0.1f);
    }

    Debug.Log($"[Unity] Initializing PlaybackManager with path: {destPath}");

        if (TVMPlaybackPlugin.InitializePlaybackManager(destPath, preLoadWindow, decodeWindow, enableLogging))
        {
            SetupMesh();
            Debug.Log($"[Unity] ✅ Playing sequence 1");
            nextToLoad = preLoadWindow + 1;
            nextToDecode = decodeWindow + 1; 

#if UNITY_ANDROID && !UNITY_EDITOR
            // On Quest, wait until we have enough buffer before starting playback
            Debug.Log("[Unity] Waiting for buffer to fill on Quest...");
            
            // We need at least 2 seconds of buffer (subsequence 1 is ~0.33s at 30fps)
            // So wait until we're confident subsequence 2 is loaded
            // Since we can't check directly, use empirical timing
            yield return new WaitForSeconds(8.0f);  // Adjust based on your Quest loading times
            playbackManagerReady = true; 
            Debug.Log("[Unity] Starting playback with buffer");
#else
            // PC is fast enough, minimal wait
            playbackManagerReady = true; 
            yield return new WaitForSeconds(0.1f);
#endif
        }
        else
        {
            Debug.LogError($"[Unity] Failed to initialize PlaybackManager with path: {destPath}");
        }
}


    void LoadAsync(int i)
    {
        System.Threading.Tasks.Task.Run(() =>
        {
            TVMPlaybackPlugin.LoadSubSequence(i);  // Use captured value
        });
    }

        void DecodeAsync(int i)
    {
        System.Threading.Tasks.Task.Run(() =>
        {
            TVMPlaybackPlugin.DecodeSubSequence(i);  // Use captured value
        });
    }

    void Update()
    {
        if (!playbackManagerReady || totalFrames <= 0)
            return;

        playbackTimer += Time.deltaTime;
        if (playbackTimer >= 1f / playbackFPS)
        {
            playbackTimer -= 1f / playbackFPS;
            currentFrame++;

            // Check if we need to advance subsequence
            if (currentFrame >= totalFrames)
            {
                if (!TVMPlaybackPlugin.AdvanceSubSequence())
                {
                    Debug.Log($"[Unity] Waiting...");
                    return;
                }
                currentFrame = 0;
                SetupMesh();
                if (currentSubsequenceIndex < TVMPlaybackPlugin.getSubSequenceCount())
                {
                    currentSubsequenceIndex++;
                }
                else
                {
                    currentSubsequenceIndex = 1;
                }
                totalFrames = TVMPlaybackPlugin.GetCurrentDecoderTotalFrames();
                // Load and decode the next subsequences
                Debug.Log($"[Unity] ✅Playing sequence {currentSubsequenceIndex}");
                LoadAsync(nextToLoad);
                if (nextToLoad < TVMPlaybackPlugin.getSubSequenceCount())
                {
                    nextToLoad++;
                }
                else
                {
                    nextToLoad = 1;
                }
                DecodeAsync(nextToDecode);
                if (nextToDecode < TVMPlaybackPlugin.getSubSequenceCount())
                {
                    nextToDecode++;
                }
                else
                {
                    nextToDecode = 1;
                }
            }
            else
            {
                UpdateMeshFromDecoder(currentFrame);
            }
        }
    }

    void SetupMesh()
    {
        totalFrames = TVMPlaybackPlugin.GetCurrentDecoderTotalFrames();
        vertexCount = TVMPlaybackPlugin.GetCurrentDecoderVertexCount();
        //Setup the mesh frame 0.
        float[] baseVerts = new float[vertexCount * 3];
        TVMPlaybackPlugin.FetchFrame(0, baseVerts);
        vertexArray = new Vector3[vertexCount];
        for (int i = 0; i < vertexCount; i++)
        {
            int idx = i * 3;
            vertexArray[i] = new Vector3(baseVerts[idx], baseVerts[idx + 1], baseVerts[idx + 2]);
        }
        //Set new values and components for new mesh.
        int triangleCount = TVMPlaybackPlugin.GetCurrentDecoderTriangleIndexCount();
        triangleIndices = new int[triangleCount];
        TVMPlaybackPlugin.GetCurrentDecoderTriangleIndices(triangleIndices, triangleCount);

        if (meshFilter == null)
            meshFilter = gameObject.GetComponent<MeshFilter>() ?? gameObject.AddComponent<MeshFilter>();
        MeshRenderer renderer = gameObject.GetComponent<MeshRenderer>() ?? gameObject.AddComponent<MeshRenderer>();
        renderer.material = sequenceMat;

        Mesh mesh = meshFilter.sharedMesh;
        if (mesh == null)
        {
            mesh = new Mesh
            {
                name = "RuntimeNativeMesh",
                indexFormat = UnityEngine.Rendering.IndexFormat.UInt32
            };
            mesh.MarkDynamic();
            meshFilter.sharedMesh = mesh;
        }
        else
        {
            mesh.Clear();
        }
        mesh.vertices = vertexArray;
        mesh.triangles = triangleIndices;
        //Recaclulate shading.
        mesh.RecalculateNormals();
        mesh.RecalculateBounds();
        Debug.Log($"[Unity] Mesh for sequence {currentSubsequenceIndex} setup: {vertexCount} vertices, {triangleCount / 3} triangles, {totalFrames} frames");
    }

    void UpdateMeshFromDecoder(int frame)
    {
        float[] frameData = new float[vertexCount * 3];
        TVMPlaybackPlugin.FetchFrame(frame, frameData);

        if (frameData.Length < 3 || float.IsNaN(frameData[0]))
        {
            Debug.LogError("[Unity] Invalid frame data.");
            return;
        }

        for (int i = 0; i < vertexCount; i++)
        {
            int idx = i * 3;
            vertexArray[i] = new Vector3(frameData[idx], frameData[idx + 1], frameData[idx + 2]);
        }
        Mesh mesh = meshFilter.sharedMesh;
        mesh.vertices = vertexArray;
        mesh.RecalculateNormals();
        mesh.RecalculateBounds();
    }
}