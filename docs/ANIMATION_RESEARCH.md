# Animation & Motion Capture Research for LIME Editor

## Status: TODO — Research collected Feb 16, 2026

---

## 1. AI Text-to-Motion Generation

### HY-Motion 1.0 (Tencent, Dec 2025) — TOP PRIORITY
- **What**: 1B parameter model, text prompt → skeleton animation
- **Why**: Same Tencent team as Hunyuan3D (already integrated in LIME)
- **Input**: English text prompt under 60 words (e.g., "a person walks forward and waves")
- **Output**: Skeleton-based 3D motion data
- **Requirements**: ~24GB VRAM, ~50GB disk, PyTorch
- **Has lighter "Lite" 0.46B version**
- **Runs locally**: CLI or Gradio web UI (port 7860)
- **GitHub**: https://github.com/Tencent-Hunyuan/HY-Motion-1.0

### MDM — Motion Diffusion Model
- **What**: Research model, text → joint position sequences
- **Output**: Joint positions (.npy), convertible to BVH
- **Lighter weight, well-documented**
- **Extensions**: PriorMDM for chaining multiple actions together
- **GitHub**: https://github.com/GuyTevet/motion-diffusion-model
- **PriorMDM**: https://github.com/priorMDM/priorMDM

### MotionGPT (NeurIPS 2023)
- **What**: Treats motion as "language", LLM generates motion tokens
- **Bidirectional**: text→motion AND motion→text (describe what a motion does)
- **GitHub**: https://github.com/OpenMotionLab/MotionGPT

### DeepMotion SayMotion (Commercial)
- **What**: Cloud API, text prompt → animation
- **Note**: Not local, subscription-based
- **URL**: https://www.deepmotion.com/saymotion

---

## 2. Video-to-Motion Capture (Extract 3D motion from 2D video)

### Video2BVH — Direct video to BVH pipeline
- **What**: 3 modules: 2D pose estimation (OpenPose) → 3D lifting → BVH export
- **Output**: BVH files ready for import
- **GitHub**: https://github.com/KevinLTT/video2bvh

### FrankMocap (Meta/Facebook Research)
- **What**: Single-view 3D mocap — body, hands, or body+hands
- **Easy to use, state-of-the-art accuracy**
- **GitHub**: https://github.com/facebookresearch/frankmocap

### EasyMocap (Zhejiang University)
- **What**: Multi-view and single-view human motion capture
- **Designed to be accessible**
- **GitHub**: https://github.com/zju3dv/EasyMocap

### Pose2Sim
- **What**: Free, open-source, research-grade accuracy
- **Markerless kinematics from any cameras → OpenSim motion**
- **Low-cost hardware requirements**
- **GitHub**: https://github.com/perfanalytics/pose2sim

### BioPose (WACV 2025)
- **What**: Biomechanically-accurate 3D pose from monocular video
- **Paper**: https://arxiv.org/abs/2501.07800

---

## 3. DIY Mocap Studio Options

### Cheapest: AI/Vision-Based (No suit needed)
- **Remocapp**: High-quality mocap from just 2 webcams, outperforms some expensive hardware
  - https://remocapp.com
- **Move AI**: Markerless, uses AI + computer vision, just a camera needed
- **FrankMocap/Video2BVH**: Free, just need a phone camera or webcam

### Mid-Range: Sensor Suits ($300-$1500)
- **Rokoko Smartsuit**: Inertial sensors, no external cameras needed
  - Portable: laptop + suit, ready in minutes
  - Indie creator bundle with 30% discount
  - https://www.rokoko.com/mocap/indie-creator-bundle
- **DIY sensor kit**: 4 cameras + inertial sensors + tracking software

### Key Advantages for Indie Dev
- 2-3 person crew max (used to require a team)
- Portable — can capture on location
- No need for expensive optical marker systems anymore

---

## 4. Integration Path for LIME Editor

### Phase 1: Keyframe/Timeline System
- Basic keyframe recording (snapshot bone positions/rotations at frame N)
- Timeline scrub bar, play/pause, frame range
- Lerp positions, slerp rotations between keyframes

### Phase 2: BVH Import
- Parse BVH files (text format: hierarchy + per-frame rotations)
- Retarget to LIME skeleton (map BVH bone names → our bone indices)
- Bake to keyframes
- Opens up: Mixamo library, CMU mocap database, any BVH source

### Phase 3: AI Motion Generation (HY-Motion)
- Backend endpoint similar to Hunyuan3D integration
- Send text prompt → receive motion data → retarget to skeleton → bake keyframes
- Could run as local service on port 7860

### Phase 4: Video-to-Motion
- Integrate Video2BVH or FrankMocap as backend service
- Drop in a video clip → extract BVH → import into LIME
- Record yourself with phone → instant mocap

---

## Awesome Collections
- https://github.com/Zilize/awesome-text-to-motion — Curated list of text-to-motion papers, datasets, models
