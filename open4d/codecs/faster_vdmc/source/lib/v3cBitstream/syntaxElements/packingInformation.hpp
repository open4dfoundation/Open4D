#pragma once

#include "v3cCommon.hpp"

namespace vmesh {

  // Like gsl::at but prints a message before abnormal program termination and supports nested arrays
template <typename Container>
constexpr auto at(Container &container, size_t index) noexcept -> decltype(auto) {
  assert(index < std::size(container));
  // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index)
  return container[index];
}

// Like gsl::at but prints a message before abnormal program termination and supports nested arrays
template <typename T>
constexpr auto at(const std::initializer_list<T> init, size_t index) noexcept -> decltype(auto) {
  assert(index < init.size());
  return *(init.begin() + index);
}

// Like gsl::at but prints a message before abnormal program termination and supports nested arrays
template <typename Container, typename... SizeT>
constexpr auto at(Container &container, size_t index0, SizeT... index) noexcept -> decltype(auto) {
  return at(at(container, index0), index...);
}

}

namespace vmesh {

enum class AiAttributeTypeId : uint8_t {
  ATTR_TEXTURE,
  ATTR_MATERIAL_ID,
  ATTR_TRANSPARENCY,
  ATTR_REFLECTANCE,
  ATTR_NORMAL,
  ATTR_UNSPECIFIED = 15
};

struct PinRegion {
  auto operator==(const PinRegion &other) const noexcept -> bool {
    return (pin_region_tile_id == other.pin_region_tile_id) &&
           (pin_region_type_id_minus2 == other.pin_region_type_id_minus2) &&
           (pin_region_top_left_x == other.pin_region_top_left_x) &&
           (pin_region_top_left_y == other.pin_region_top_left_y) &&
           (pin_region_width_minus1 == other.pin_region_width_minus1) &&
           (pin_region_height_minus1 == other.pin_region_height_minus1) &&
           (pin_region_unpack_top_left_x == other.pin_region_unpack_top_left_x) &&
           (pin_region_unpack_top_left_y == other.pin_region_unpack_top_left_y) &&
           (pin_region_rotation_flag == other.pin_region_rotation_flag) &&
           (pin_region_map_index == other.pin_region_map_index) &&
           (pin_region_auxiliary_data_flag == other.pin_region_auxiliary_data_flag) &&
           (pin_region_attr_index == other.pin_region_attr_index) &&
           (pin_region_attr_partition_index == other.pin_region_attr_partition_index);
  }

  uint8_t  pin_region_tile_id              = 0;
  uint8_t  pin_region_type_id_minus2       = 0;
  uint16_t pin_region_top_left_x           = 0;
  uint16_t pin_region_top_left_y           = 0;
  uint16_t pin_region_width_minus1         = 0;
  uint16_t pin_region_height_minus1        = 0;
  uint16_t pin_region_unpack_top_left_x    = 0;
  uint16_t pin_region_unpack_top_left_y    = 0;
  bool     pin_region_rotation_flag        = false;
  uint8_t  pin_region_map_index            = 0;
  bool     pin_region_auxiliary_data_flag  = false;
  uint8_t  pin_region_attr_index           = 0;
  uint8_t  pin_region_attr_partition_index = 0;

  [[nodiscard]] constexpr auto pinRegionTypeId() const noexcept {
    return static_cast<vmesh::V3CUnitType>(pin_region_type_id_minus2 + 2);
  }
};
struct PinAttributeInformation {
  auto operator==(const PinAttributeInformation &other) const noexcept -> bool {
    return (pin_attribute_type_id == other.pin_attribute_type_id) &&
           (pin_attribute_2d_bit_depth_minus1 == other.pin_attribute_2d_bit_depth_minus1) &&
           (pin_attribute_MSB_align_flag == other.pin_attribute_MSB_align_flag) &&
           (pin_attribute_map_absolute_coding_persistence_flag ==
            other.pin_attribute_map_absolute_coding_persistence_flag) &&
           (pin_attribute_dimension_minus1 == other.pin_attribute_dimension_minus1) &&
           (pin_attribute_dimension_partitions_minus1 ==
            other.pin_attribute_dimension_partitions_minus1) &&
           (pin_attribute_partition_channels_minus1 ==
            other.pin_attribute_partition_channels_minus1);
  }


  uint8_t pin_attribute_2d_bit_depth_minus1               = 0;
  bool pin_attribute_MSB_align_flag                       = false;
  bool pin_attribute_map_absolute_coding_persistence_flag = false;
  uint8_t pin_attribute_dimension_minus1                  = 0;
  uint8_t pin_attribute_dimension_partitions_minus1       = 0;
  std::vector<uint8_t> pin_attribute_partition_channels_minus1;
  AiAttributeTypeId pin_attribute_type_id = AiAttributeTypeId::ATTR_TEXTURE;
};

class PackingInformation {
public:
  [[nodiscard]] auto pin_codec_id() const noexcept -> uint8_t;
  [[nodiscard]] auto pin_occupancy_present_flag() const -> bool;
  [[nodiscard]] auto pin_geometry_present_flag() const -> bool;
  [[nodiscard]] auto pin_attribute_present_flag() const -> bool;
  [[nodiscard]] auto pin_occupancy_2d_bit_depth_minus1() const -> uint8_t;
  [[nodiscard]] auto pin_occupancy_MSB_align_flag() const -> bool;
  [[nodiscard]] auto pin_lossy_occupancy_compression_threshold() const -> uint8_t;
  [[nodiscard]] auto pin_geometry_2d_bit_depth_minus1() const -> uint8_t;
  [[nodiscard]] auto pin_geometry_MSB_align_flag() const -> bool;
  [[nodiscard]] auto pin_geometry_3d_coordinates_bit_depth_minus1() const -> uint8_t;
  [[nodiscard]] auto pin_attribute_count() const -> uint8_t;
  [[nodiscard]] auto pin_attribute_type_id(size_t i) const -> AiAttributeTypeId;
  [[nodiscard]] auto pin_attribute_2d_bit_depth_minus1(size_t i) const -> uint8_t;
  [[nodiscard]] auto pin_attribute_MSB_align_flag(size_t i) const -> bool;
  [[nodiscard]] auto pin_attribute_map_absolute_coding_persistence_flag(size_t i) const -> bool;
  [[nodiscard]] auto pin_attribute_dimension_minus1(size_t i) const -> uint8_t;
  [[nodiscard]] auto pin_attribute_dimension_partitions_minus1(size_t i) const -> uint8_t;
  [[nodiscard]] auto pin_attribute_partition_channels_minus1(size_t i, uint8_t l) const -> uint8_t;
  [[nodiscard]] auto pin_regions_count_minus1() const -> uint8_t;
  [[nodiscard]] auto pin_region_tile_id(size_t i) const -> uint8_t;
  [[nodiscard]] auto pin_region_type_id_minus2(size_t i) const -> uint8_t;
  [[nodiscard]] auto pinRegionTypeId(size_t i) const ->  vmesh::V3CUnitType;
  [[nodiscard]] auto pin_region_top_left_x(size_t i) const -> uint16_t;
  [[nodiscard]] auto pin_region_top_left_y(size_t i) const -> uint16_t;
  [[nodiscard]] auto pin_region_width_minus1(size_t i) const -> uint16_t;
  [[nodiscard]] auto pin_region_height_minus1(size_t i) const -> uint16_t;
  [[nodiscard]] auto pin_region_unpack_top_left_x(size_t i) const -> uint16_t;
  [[nodiscard]] auto pin_region_unpack_top_left_y(size_t i) const -> uint16_t;
  [[nodiscard]] auto pin_region_rotation_flag(size_t i) const -> bool;
  [[nodiscard]] auto pin_region_map_index(size_t i) const -> uint8_t;
  [[nodiscard]] auto pin_region_auxiliary_data_flag(size_t i) const -> bool;
  [[nodiscard]] auto pin_region_attr_index(size_t i) const -> uint8_t;
  [[nodiscard]] auto pin_region_attr_partition_index(size_t i) const -> uint8_t;

  constexpr auto pin_codec_id(uint8_t value) noexcept -> auto &;
  auto pin_occupancy_present_flag(bool value) -> auto &;
  auto pin_geometry_present_flag(bool value) -> auto &;
  auto pin_attribute_present_flag(bool value) -> auto &;
  auto pin_occupancy_2d_bit_depth_minus1(uint8_t value) -> auto &;
  auto pin_occupancy_MSB_align_flag(bool value) -> auto &;
  auto pin_lossy_occupancy_compression_threshold(uint8_t value) -> auto &;
  auto pin_geometry_2d_bit_depth_minus1(uint8_t value) -> auto &;
  auto pin_geometry_MSB_align_flag(bool value) -> auto &;
  auto pin_geometry_3d_coordinates_bit_depth_minus1(uint8_t value) -> auto &;
  auto pin_attribute_allocate(uint8_t value) -> auto &;
  auto pin_attribute_count(uint8_t value) -> auto &;
  auto pin_attribute_type_id(size_t i, AiAttributeTypeId value) -> auto &;
  auto pin_attribute_2d_bit_depth_minus1(size_t i, uint8_t value) -> auto &;
  auto pin_attribute_MSB_align_flag(size_t i, bool value) -> auto &;
  auto pin_attribute_map_absolute_coding_persistence_flag(size_t i, bool value) -> auto &;
  auto pin_attribute_dimension_minus1(size_t i, uint8_t value) -> auto &;
  auto pin_attribute_dimension_partitions_minus1(size_t i, uint8_t value) -> auto &;
  auto pin_attribute_partition_channels_minus1(size_t i, uint8_t l, uint8_t value) -> auto &;
  auto pin_regions_count_minus1(uint8_t value) -> auto &;
  auto pin_region_tile_id(size_t i, uint8_t value) -> auto &;
  auto pin_region_type_id_minus2(size_t i, uint8_t value) -> auto &;
  auto pinRegionTypeId(size_t i,  vmesh::V3CUnitType value) -> auto &;
  auto pin_region_top_left_x(size_t i, uint16_t value) -> auto &;
  auto pin_region_top_left_y(size_t i, uint16_t value) -> auto &;
  auto pin_region_width_minus1(size_t i, uint16_t value) -> auto &;
  auto pin_region_height_minus1(size_t i, uint16_t value) -> auto &;
  auto pin_region_unpack_top_left_x(size_t i, uint16_t value) -> auto &;
  auto pin_region_unpack_top_left_y(size_t i, uint16_t value) -> auto &;
  auto pin_region_map_index(size_t i, uint8_t value) -> auto &;
  auto pin_region_rotation_flag(size_t i, bool value) -> auto &;
  auto pin_region_auxiliary_data_flag(size_t i, bool value) -> auto &;
  auto pin_region_attr_index(size_t i, uint8_t value) -> auto &;
  auto pin_region_attr_partition_index(size_t i, uint8_t value) -> auto &;

  auto operator==(const PackingInformation &other) const noexcept -> bool;
  auto operator!=(const PackingInformation &other) const noexcept -> bool;

  // static auto decodeFrom(Common::InputBitstream &bitstream) -> PackingInformation;

  // void encodeTo(Common::OutputBitstream &bitstream) const;


private:
  uint8_t m_pin_codec_id             = 0;
  uint8_t m_pin_attribute_count      = 0;
  uint8_t m_pin_regions_count_minus1 = 0;
  bool m_pin_occupancy_present_flag                      = false;
  bool m_pin_geometry_present_flag                       = false;
  bool m_pin_attribute_present_flag                      = false;
  uint8_t m_pin_occupancy_2d_bit_depth_minus1            = 0;
  bool m_pin_occupancy_MSB_align_flag                    = false;
  uint8_t m_pin_lossy_occupancy_compression_threshold    = 0;
  uint8_t m_pin_geometry_2d_bit_depth_minus1             = 0;
  bool m_pin_geometry_MSB_align_flag                     = false;
  uint8_t m_pin_geometry_3d_coordinates_bit_depth_minus1 = 0;
  std::vector<PinAttributeInformation> m_pinAttributeInformation;
  std::vector<PinRegion> m_pinRegions{std::vector<PinRegion>(1U)};
};
constexpr auto PackingInformation::pin_codec_id(uint8_t value) noexcept -> auto & {
  m_pin_codec_id = value;
  return *this;
}
inline auto PackingInformation::pin_occupancy_present_flag(bool value) -> auto & {
  m_pin_occupancy_present_flag = value;
  return *this;
}

inline auto PackingInformation::pin_geometry_present_flag(bool value) -> auto & {
  m_pin_geometry_present_flag = value;
  return *this;
}

inline auto PackingInformation::pin_attribute_present_flag(bool value) -> auto & {
  m_pin_attribute_present_flag = value;
  return *this;
}

inline auto PackingInformation::pin_regions_count_minus1(uint8_t value) -> auto & {
  m_pin_regions_count_minus1 = value;
  m_pinRegions = std::vector<PinRegion>(value + 1U);
  return *this;
}

inline auto PackingInformation::pin_region_tile_id(size_t i, uint8_t value) -> auto & {
  assert(i <= pin_regions_count_minus1());
  m_pinRegions[i].pin_region_tile_id = value;
  return *this;
}

inline auto PackingInformation::pin_region_type_id_minus2(size_t i, uint8_t value) -> auto & {
  assert(i <= pin_regions_count_minus1());
  m_pinRegions[i].pin_region_type_id_minus2 = value;

  switch (pinRegionTypeId(i)) {
  case  vmesh::V3CUnitType::V3C_AVD:
    return pin_attribute_present_flag(true);
  case  vmesh::V3CUnitType::V3C_GVD:
    return pin_geometry_present_flag(true);
  case  vmesh::V3CUnitType::V3C_OVD:
    return pin_occupancy_present_flag(true);
  default:
    return *this;
  }
}

// inline auto PackingInformation::pinRegionTypeId(size_t i,  vmesh::V3CUnitType value) -> auto & {
//   assert(value !=  vmesh::V3CUnitType::V3C_VPS && value !=  vmesh::V3CUnitType::V3C_AD);
//   const auto typeIdMinus2 = static_cast<int32_t>(value) - 2;
//   return pin_region_type_id_minus2(i, Common::assertDownCast<uint8_t>(typeIdMinus2));
// }


inline auto PackingInformation::pin_region_top_left_x(size_t i, uint16_t value) -> auto & {
  assert(i <= pin_regions_count_minus1());
  m_pinRegions[i].pin_region_top_left_x = value;
  return *this;
}

inline auto PackingInformation::pin_region_top_left_y(size_t i, uint16_t value) -> auto & {
  assert(i <= pin_regions_count_minus1());
  m_pinRegions[i].pin_region_top_left_y = value;
  return *this;
}

inline auto PackingInformation::pin_region_width_minus1(size_t i, uint16_t value) -> auto & {
  assert(i <= pin_regions_count_minus1());
  m_pinRegions[i].pin_region_width_minus1 = value;
  return *this;
}

inline auto PackingInformation::pin_region_height_minus1(size_t i, uint16_t value) -> auto & {
  assert(i <= pin_regions_count_minus1());
  m_pinRegions[i].pin_region_height_minus1 = value;
  return *this;
}

inline auto PackingInformation::pin_region_unpack_top_left_x(size_t i, uint16_t value) -> auto & {
  assert(i <= pin_regions_count_minus1());
  m_pinRegions[i].pin_region_unpack_top_left_x = value;
  return *this;
}

inline auto PackingInformation::pin_region_unpack_top_left_y(size_t i, uint16_t value) -> auto & {
  assert(i <= pin_regions_count_minus1());
  m_pinRegions[i].pin_region_unpack_top_left_y = value;
  return *this;
}

inline auto PackingInformation::pin_region_map_index(size_t i, uint8_t value) -> auto & {
  assert(i <= pin_regions_count_minus1());
  m_pinRegions[i].pin_region_map_index = value;
  return *this;
}

inline auto PackingInformation::pin_region_rotation_flag(size_t i, bool value) -> auto & {
  assert(i <= pin_regions_count_minus1());
  m_pinRegions[i].pin_region_rotation_flag = value;
  return *this;
}

inline auto PackingInformation::pin_region_auxiliary_data_flag(size_t i, bool value) -> auto & {
  assert(i <= pin_regions_count_minus1());
  m_pinRegions[i].pin_region_auxiliary_data_flag = value;
  return *this;
}

inline auto PackingInformation::pin_region_attr_index(size_t i, uint8_t value) -> auto & {
  assert(i <= pin_regions_count_minus1());
  assert(value <= 4);
  m_pinRegions[i].pin_region_attr_index = value;
  return *this;
}

inline auto PackingInformation::pin_region_attr_partition_index(size_t i, uint8_t value) -> auto & {
  assert(i <= pin_regions_count_minus1());
  m_pinRegions[i].pin_region_attr_partition_index = value;
  return *this;
}

inline auto PackingInformation::pin_occupancy_2d_bit_depth_minus1(uint8_t value) -> auto & {
  pin_occupancy_present_flag(true);
  m_pin_occupancy_2d_bit_depth_minus1 = value;
  return *this;
}

inline auto PackingInformation::pin_occupancy_MSB_align_flag(bool value) -> auto & {
  pin_occupancy_present_flag(true);
  m_pin_occupancy_MSB_align_flag = value;
  return *this;
}

inline auto PackingInformation::pin_lossy_occupancy_compression_threshold(uint8_t value) -> auto & {
  pin_occupancy_present_flag(true);
  m_pin_lossy_occupancy_compression_threshold = value;
  return *this;
}

inline auto PackingInformation::pin_geometry_2d_bit_depth_minus1(uint8_t value) -> auto & {
  pin_geometry_present_flag(true);
  m_pin_geometry_2d_bit_depth_minus1 = value;
  return *this;
}

inline auto PackingInformation::pin_geometry_MSB_align_flag(bool value) -> auto & {
  pin_geometry_present_flag(true);
  m_pin_geometry_MSB_align_flag = value;
  return *this;
}

inline auto PackingInformation::pin_geometry_3d_coordinates_bit_depth_minus1(uint8_t value)
    -> auto & {
  pin_geometry_present_flag(true);
  m_pin_geometry_3d_coordinates_bit_depth_minus1 = value;
  return *this;
}

inline auto PackingInformation::pin_attribute_count(uint8_t value) -> auto & {
  pin_attribute_present_flag(true);
  m_pin_attribute_count = value;
  m_pinAttributeInformation = std::vector<PinAttributeInformation>(value);
  return *this;
}


inline auto PackingInformation::pin_attribute_type_id(size_t i, AiAttributeTypeId value) -> auto & {
  assert(i < pin_attribute_count());
  m_pinAttributeInformation[i].pin_attribute_type_id = value;
  return *this;
}

inline auto PackingInformation::pin_attribute_2d_bit_depth_minus1(size_t i, uint8_t value)
    -> auto & {
  assert(i < pin_attribute_count());
  m_pinAttributeInformation[i].pin_attribute_2d_bit_depth_minus1 = value;
  return *this;
}

inline auto PackingInformation::pin_attribute_MSB_align_flag(size_t i, bool value) -> auto & {
  assert(i < pin_attribute_count());
  m_pinAttributeInformation[i].pin_attribute_MSB_align_flag = value;
  return *this;
}

inline auto PackingInformation::pin_attribute_map_absolute_coding_persistence_flag(size_t i,
                                                                                   bool value)
    -> auto & {
  assert(i < pin_attribute_count());
  m_pinAttributeInformation[i]
      .pin_attribute_map_absolute_coding_persistence_flag = value;
  return *this;
}

inline auto PackingInformation::pin_attribute_dimension_minus1(size_t i, uint8_t value) -> auto & {
  assert(i < pin_attribute_count());
  m_pinAttributeInformation[i].pin_attribute_dimension_minus1 = value;
  return *this;
}

inline auto PackingInformation::pin_attribute_dimension_partitions_minus1(size_t i, uint8_t value)
    -> auto & {
  assert(i < pin_attribute_count());
  m_pinAttributeInformation[i].pin_attribute_dimension_partitions_minus1 =
      value;
  m_pinAttributeInformation[i].pin_attribute_partition_channels_minus1 =
      std::vector<uint8_t>(value + 1U);
  return *this;
}

inline auto PackingInformation::pin_attribute_partition_channels_minus1(size_t i, uint8_t l,
                                                                        uint8_t value) -> auto & {
  assert(i < pin_attribute_count());
  assert(l < m_pinAttributeInformation[i]
                              .pin_attribute_partition_channels_minus1
                              .size());
  m_pinAttributeInformation[i].pin_attribute_partition_channels_minus1[l] = value;
  return *this;
}

inline auto PackingInformation::pin_codec_id() const noexcept -> uint8_t { return m_pin_codec_id; }

inline auto PackingInformation::pin_regions_count_minus1() const -> uint8_t {
  assert(m_pin_attribute_count || m_pin_geometry_present_flag ||
                      m_pin_occupancy_present_flag);
  return m_pin_regions_count_minus1;
}

inline auto PackingInformation::pin_region_tile_id(size_t i) const -> uint8_t {
  assert(i <= pin_regions_count_minus1());
  return m_pinRegions[i].pin_region_tile_id;
}

inline auto PackingInformation::pin_region_type_id_minus2(size_t i) const -> uint8_t {
  assert(i <= pin_regions_count_minus1());
  return m_pinRegions[i].pin_region_type_id_minus2;
}

inline auto PackingInformation::pinRegionTypeId(size_t i) const -> V3CUnitType {
  return static_cast<V3CUnitType>(pin_region_type_id_minus2(i) + 2);
}

inline auto PackingInformation::pin_region_top_left_x(size_t i) const -> uint16_t {
  assert(i <= pin_regions_count_minus1());
  return m_pinRegions[i].pin_region_top_left_x;
}

inline auto PackingInformation::pin_region_top_left_y(size_t i) const -> uint16_t {
  assert(i <= pin_regions_count_minus1());
  return m_pinRegions[i].pin_region_top_left_y;
}

inline auto PackingInformation::pin_region_width_minus1(size_t i) const -> uint16_t {
  assert(i <= pin_regions_count_minus1());
  return m_pinRegions[i].pin_region_width_minus1;
}

inline auto PackingInformation::pin_region_height_minus1(size_t i) const -> uint16_t {
  assert(i <= pin_regions_count_minus1());
  return m_pinRegions[i].pin_region_height_minus1;
}

inline auto PackingInformation::pin_region_unpack_top_left_x(size_t i) const -> uint16_t {
  assert(i <= pin_regions_count_minus1());
  return m_pinRegions[i].pin_region_unpack_top_left_x;
}

inline auto PackingInformation::pin_region_unpack_top_left_y(size_t i) const -> uint16_t {
  assert(i <= pin_regions_count_minus1());
  return m_pinRegions[i].pin_region_unpack_top_left_y;
}

inline auto PackingInformation::pin_region_rotation_flag(size_t i) const -> bool {
  assert(i <= pin_regions_count_minus1());
  return m_pinRegions[i].pin_region_rotation_flag;
}

inline auto PackingInformation::pin_region_map_index(size_t i) const -> uint8_t {
  assert(i <= pin_regions_count_minus1());
  return m_pinRegions[i].pin_region_map_index;
}

inline auto PackingInformation::pin_region_auxiliary_data_flag(size_t i) const -> bool {
  assert(i <= pin_regions_count_minus1() &&
                      m_pinRegions[i].pin_region_auxiliary_data_flag);
  return m_pinRegions[i].pin_region_auxiliary_data_flag;
}

inline auto PackingInformation::pin_region_attr_index(size_t i) const -> uint8_t {
  assert(i <= pin_regions_count_minus1() && m_pinRegions[i].pin_region_attr_index);
  return m_pinRegions[i].pin_region_attr_index;
}

inline auto PackingInformation::pin_region_attr_partition_index(size_t i) const -> uint8_t {
  assert(i <= pin_regions_count_minus1());
  return m_pinRegions[i].pin_region_attr_partition_index ? m_pinRegions[i].pin_region_attr_partition_index : 0;
}

inline auto PackingInformation::pin_occupancy_present_flag() const -> bool {
  return m_pin_occupancy_present_flag;
}

inline auto PackingInformation::pin_geometry_present_flag() const -> bool {
  return m_pin_geometry_present_flag;
}

inline auto PackingInformation::pin_attribute_present_flag() const -> bool {
  return m_pin_attribute_present_flag;
}

inline auto PackingInformation::pin_occupancy_2d_bit_depth_minus1() const -> uint8_t {
  return m_pin_occupancy_2d_bit_depth_minus1;
}

inline auto PackingInformation::pin_occupancy_MSB_align_flag() const -> bool {
  return m_pin_occupancy_MSB_align_flag;
}

inline auto PackingInformation::pin_lossy_occupancy_compression_threshold() const -> uint8_t {
  return m_pin_lossy_occupancy_compression_threshold;
}

inline auto PackingInformation::pin_geometry_2d_bit_depth_minus1() const -> uint8_t {
  return m_pin_geometry_2d_bit_depth_minus1;
}

inline auto PackingInformation::pin_geometry_MSB_align_flag() const -> bool {
  return m_pin_geometry_MSB_align_flag;
}

inline auto PackingInformation::pin_geometry_3d_coordinates_bit_depth_minus1() const -> uint8_t {
  return m_pin_geometry_3d_coordinates_bit_depth_minus1;
}

inline auto PackingInformation::pin_attribute_count() const -> uint8_t {
  return m_pin_attribute_count;
}

inline auto PackingInformation::pin_attribute_type_id(size_t i) const -> AiAttributeTypeId {
  assert(i < m_pinAttributeInformation.size());
  return m_pinAttributeInformation[i].pin_attribute_type_id;
}

inline auto PackingInformation::pin_attribute_2d_bit_depth_minus1(size_t i) const -> uint8_t {
  assert(i < m_pinAttributeInformation.size());
  return m_pinAttributeInformation[i].pin_attribute_2d_bit_depth_minus1;
}

inline auto PackingInformation::pin_attribute_MSB_align_flag(size_t i) const -> bool {
  assert(i < m_pinAttributeInformation.size());
  return m_pinAttributeInformation[i].pin_attribute_MSB_align_flag;
}

inline auto PackingInformation::pin_attribute_map_absolute_coding_persistence_flag(size_t i) const
    -> bool {
  assert(i < m_pinAttributeInformation.size());
  return m_pinAttributeInformation[i].pin_attribute_map_absolute_coding_persistence_flag;
}

inline auto PackingInformation::pin_attribute_dimension_minus1(size_t i) const -> uint8_t {
  assert(i < m_pinAttributeInformation.size());
  return m_pinAttributeInformation[i].pin_attribute_dimension_minus1;
}

inline auto PackingInformation::pin_attribute_dimension_partitions_minus1(size_t i) const -> uint8_t {
  assert(i < m_pinAttributeInformation.size());
  return m_pinAttributeInformation[i].pin_attribute_dimension_partitions_minus1;
}

inline auto PackingInformation::pin_attribute_partition_channels_minus1(size_t i, uint8_t l) const
    -> uint8_t {
  assert(i < m_pinAttributeInformation.size());
  return m_pinAttributeInformation[i].pin_attribute_partition_channels_minus1[l];
}

inline auto PackingInformation::pin_attribute_allocate(uint8_t value) -> auto & {
  pin_attribute_count(value);
  for (int i = 0; i < value; i++){
    pin_attribute_type_id(i,AiAttributeTypeId::ATTR_TEXTURE);
    pin_attribute_2d_bit_depth_minus1(i,0);
    pin_attribute_MSB_align_flag(i,0);
    pin_attribute_dimension_minus1(i,0);
    pin_attribute_dimension_partitions_minus1(i,0);
  }
  return *this;
}

}    