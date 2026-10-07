#include "sekiro_steve.hpp"

namespace sekiro::render {

namespace {
mc::Vec3 toVec(const native::FVector3& v) { return {v.X, v.Y, v.Z}; }
native::FVector3 toNative(const mc::Vec3& v) { return {v.x, v.y, v.z}; }
} // namespace

bool sceneOccludes(float game_depth, float steve_view_z) {
    return mc::rig::sceneOccludes(kSekiroDepth, game_depth, steve_view_z);
}

Mat4 translation(const native::FVector3& t) { return mc::rig::translation(toVec(t)); }

Mat4 rotationY(float radians) { return mc::rig::rotationAboutAxis({0.f, 1.f, 0.f}, radians); }

Mat4 rotationFromQuat(const native::FQuat& q) { return mc::rig::rotationFromQuat({q.X, q.Y, q.Z, q.W}); }

std::array<float, 4> transform(const Mat4& m, const native::FVector3& p) { return mc::rig::transform(m, toVec(p)); }

native::FVector3 transformPoint(const Mat4& m, const native::FVector3& p) {
    return toNative(mc::rig::transformPoint(m, toVec(p)));
}

mc::rig::Camera toRigCamera(const live::LiveSample& cam) {
    return {toVec(cam.cam_right), toVec(cam.cam_up), toVec(cam.cam_forward), toVec(cam.cam_pos)};
}

Mat4 viewFromCamera(const live::LiveSample& cam) { return mc::rig::viewFromCamera(toRigCamera(cam)); }

Mat4 viewProjection(const live::LiveSample& cam, float fov_y_radians, float aspect, float z_near, float z_far) {
    return mc::rig::viewProjection(toRigCamera(cam), fov_y_radians, aspect, z_near, z_far);
}

SteveMesh buildPartMesh(mc::StevePart part) { return mc::rig::buildPartMesh(part, kSekiroBasis); }

native::FVector3 partPivot(mc::StevePart part) { return toNative(mc::rig::partPivot(part, kSekiroBasis)); }

Mat4 partMatrix(mc::StevePart part, const native::FQuat& rot, const native::FVector3& root_pos, float root_yaw) {
    return mc::rig::partMatrix(part, {rot.X, rot.Y, rot.Z, rot.W}, toVec(root_pos), root_yaw, kSekiroBasis);
}

} // namespace sekiro::render
