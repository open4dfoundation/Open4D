import json, sys
sys.path.insert(0, '/home/ryan/4DVideoStreaming')
from baselines.DeltaStream.orbitstream.protocol import (
    ConnectionHeader, HeaderObject, StreamMode, encode_connection,
    Frame, FrameRecord, FrameType, MotionRecord, encode_frame,
    Feedback, TileSelection, encode_feedback,
    TileAbrCatalog, TileAbrObject, TileAbrTile)
from baselines.DeltaStream.orbitstream.manifest import CameraCalibration

cam = CameraCalibration(camera_id=2, width=640, height=480, fx=525.0, fy=524.5,
                        cx=319.5, cy=239.5,
                        camera_to_world=((1,0,0,0.5),(0,1,0,1.5),(0,0,1,-2.25),(0,0,0,1)))
hdr = ConnectionHeader(mode=StreamMode.METASTREAM, width=640, height=480,
                       fps_num=30000, fps_den=1000, block_size=16,
                       objects=(HeaderObject(object_id=7, name="dancer",
                                             loop_frames=300, cameras=(cam,)),),
                       calibration_hash="abc123")

tiles = TileAbrCatalog(grid=4, representation_ratios=(1.0, 0.5, 0.25),
                       objects=(TileAbrObject(object_id=7,
                                              bounds_min=(-1.0, 0.0, -1.0),
                                              bounds_max=(1.0, 2.0, 1.0),
                                              tiles=(TileAbrTile(tile_id=0, point_count=1000,
                                                                 representation_bytes=(9000, 4500, 2250)),)),))
hdr_vivo = ConnectionHeader(mode=StreamMode.VIVO, width=640, height=480,
                            fps_num=30000, fps_den=1000, block_size=16,
                            objects=(HeaderObject(7, "dancer", 300, (cam,)),),
                            calibration_hash="abc123", tile_abr=tiles)

frame = Frame(frame_id=42, source_timestamp_ns=1234567890123,
              encode_finished_ns=1234567890999, frame_type=FrameType.DELTA,
              records=(FrameRecord(object_id=7, camera_id=2,
                                   draco=bytes(range(16)),
                                   removal_blocks=(3, 9),
                                   motions=(MotionRecord(5, 0.25, -0.5, 1.5, -3, 4),),
                                   point_count=20677),))
keyframe = Frame(frame_id=0, source_timestamp_ns=1, encode_finished_ns=2,
                 frame_type=FrameType.KEYFRAME,
                 records=(FrameRecord(7, 2, b'\xaa\xbb\xcc', (), (), 3),
                          FrameRecord(8, 0, b'\xde\xad', (), (), 2)))

out = {
  "connection": encode_connection(hdr).hex(),
  "connection_vivo_tiles": encode_connection(hdr_vivo).hex(),
  "frame_delta": encode_frame(frame).hex(),
  "frame_keyframe": encode_frame(keyframe).hex(),
  "feedback_minimal": encode_feedback(
      Feedback(41, 29.5, 2)).hex(),
  "feedback_extended": encode_feedback(
      Feedback(41, 29.5, 2, view_position=(1.0, 1.6, 4.0),
               view_forward=(0.0, 0.0, -1.0), bandwidth_mbps=250.5)).hex(),
  "feedback_full": encode_feedback(
      Feedback(41, 29.5, 2, view_position=(1.0, 1.6, 4.0),
               view_forward=(0.0, 0.0, -1.0), bandwidth_mbps=250.5,
               view_up=(0.0, 1.0, 0.0), vertical_fov_degrees=60.0,
               view_aspect=1.7777, view_near=0.05, view_far=200.0,
               selections=(TileSelection(7, 13, 2),), selections_present=True)).hex(),
}
print(json.dumps(out, indent=1))
