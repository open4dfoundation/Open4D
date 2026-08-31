import cv2
import os
import argparse
from tqdm import tqdm
import json
import multiprocessing
import numpy as np

parser = argparse.ArgumentParser()

parser.add_argument('--dataset_dir', type=str, required=True)
parser.add_argument('--process_num', type=int, default=40, help='change according to your server')

opt = parser.parse_args()

input_path=opt.dataset_dir

def process_one(i, data_path):
    image_path = os.path.join(data_path, 'image')
    mask_path = os.path.join(data_path, 'mask')

    if not os.path.exists(os.path.join(image_path,str(i).zfill(3)+".mp4")):
        return

    print("processing video ", os.path.join(image_path,str(i).zfill(3)+".mp4"))
    video =  os.path.join(image_path,str(i).zfill(3)+".mp4")
    mask_video= os.path.join(mask_path,str(i).zfill(3)+".mp4")

    video = cv2.VideoCapture(video)
    mask_video = cv2.VideoCapture(mask_video)
    frame_count = int(video.get(cv2.CAP_PROP_FRAME_COUNT))

    for j in tqdm(range(frame_count)):
        frame_path = os.path.join(image_path, '%d' % j)
        os.makedirs(frame_path, exist_ok=True)

        mask_frame_path = os.path.join(mask_path, '%d' % j)
        os.makedirs(mask_frame_path, exist_ok=True)

        if video.get(cv2.CAP_PROP_POS_FRAMES) != j:
            video.set(cv2.CAP_PROP_POS_FRAMES, j)

        if mask_video.get(cv2.CAP_PROP_POS_FRAMES) != j:
            mask_video.set(cv2.CAP_PROP_POS_FRAMES, j)

        ret, img = video.read()
        ret, img_mask = mask_video.read()

        if img.max() > 1:
            img = img / 255.
        if img_mask.max() > 1:
            img_mask = img_mask / 255.

        cv2.imwrite(os.path.join(mask_frame_path, 'img_%04d.png' % (i)), img_mask * 255)
        cv2.imwrite(os.path.join(frame_path, 'img_%04d.png' % (i)), img * 255)

def decode_video(input_path,cam_num=76):
    image_path = os.path.join(input_path, 'image')
    assert os.path.exists(image_path), 'no image folder, is the dataset_dir wrong'
    mask_path = os.path.join(input_path, 'mask')
    assert os.path.exists(mask_path), 'no mask folder, is the dataset_dir wrong'

    tasks = []
    for i, video in enumerate(range(cam_num)):
        p = multiprocessing.Process(target=process_one, args=( i, input_path))
        tasks.append(p)

    t_num = opt.process_num
    cnt = 0
    while cnt < len(tasks):
        for j in range(t_num):
            if (cnt+j)>=cam_num:
                continue
            tasks[cnt + j].start()

        for j in range(t_num):
            if (cnt + j) >= cam_num:
                continue
            tasks[cnt + j].join()
        cnt = cnt + t_num


def campose_to_extrinsic(camposes):
    if camposes.shape[1] != 12:
        raise Exception(" wrong campose data structure!")
        return

    res = np.zeros((camposes.shape[0], 4, 4))

    res[:, 0:3, 2] = camposes[:, 0:3]
    res[:, 0:3, 0] = camposes[:, 3:6]
    res[:, 0:3, 1] = camposes[:, 6:9]
    res[:, 0:3, 3] = camposes[:, 9:12]
    res[:, 3, 3] = 1.0

    return res


def read_intrinsics(fn_instrinsic):
    fo = open(fn_instrinsic)
    data = fo.readlines()
    i = 0
    Ks = []
    while i < len(data):
        if len(data[i]) > 5:
            tmp = data[i].split()
            tmp = [float(i) for i in tmp]
            a = np.array(tmp)
            i = i + 1
            tmp = data[i].split()
            tmp = [float(i) for i in tmp]
            b = np.array(tmp)
            i = i + 1
            tmp = data[i].split()
            tmp = [float(i) for i in tmp]
            c = np.array(tmp)
            res = np.vstack([a, b, c])
            Ks.append(res)

        i = i + 1
    Ks = np.stack(Ks)
    fo.close()

    return Ks

def gen_cam_json(input_path):
    camposes = np.loadtxt(os.path.join(input_path, 'CamPose.inf'))
    Ts = campose_to_extrinsic(camposes)
    Ks = read_intrinsics(os.path.join(input_path, 'Intrinsic.inf'))

    m = np.mean(Ts[:, :3, 3], axis=0)
    print('OBJ center:', m)
    Ts[:, :3, 3] = Ts[:, :3, 3] - m
    print(Ts[:, :3, 3].max(), -Ts[:, :3, 3].min())
    Ts[:, :3, 3] = Ts[:, :3, 3] * 2.0 / max(Ts[:, :3, 3].max(), -Ts[:, :3, 3].min())

    image_path = os.path.join(input_path, 'image')
    mask_path = os.path.join(input_path, 'mask')

    video = os.path.join(image_path, str(0).zfill(3) + ".mp4")
    video = cv2.VideoCapture(video)
    frame_count = int(video.get(cv2.CAP_PROP_FRAME_COUNT))

    tmp = 0
    for j in range(frame_count):
        frames = []
        for i in range(77):
            if not os.path.exists(os.path.join(image_path, str(i).zfill(3) + ".mp4")):
                continue
            frame = {}
            frame['file'] = image_path + '/%d/img_%04d.png' % (j, i)
            frame['mask'] = mask_path + '/%d/img_%04d.png' % (j, i)
            frame['extrinsic'] = Ts[i].tolist()
            frame['intrinsic'] = Ks[i].tolist()
            frames.append(frame)
        with open(os.path.join(input_path, 'cams_%d.json' % tmp), 'w', encoding='utf-8') as f:
            json.dump({'frames': frames}, f, ensure_ascii=False, indent=4)

        tmp += 1

    print('done.')

decode_video(input_path)

gen_cam_json(input_path)









