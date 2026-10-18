"""Owned mesh appearance stored as bounded arrays beside geometry."""

from open4d.core import TextureImage, MeshTexture
from ._protocol import CodecError


def texture_arrays(texture):
    if texture is None:
        return None, {}
    arrays, images = {}, []
    for index, image in enumerate(texture.images):
        key = f"texture_{index:04d}"
        arrays[key] = image.pixels
        images.append({"array": key, "color_space": image.color_space,
                       "wrap": image.wrap, "filter": image.filter, "name": image.name})
    if texture.material_ids is not None:
        arrays["texture_material_ids"] = texture.material_ids
    return {"images": images, "material_names": list(texture.material_names),
            "material_ids": "texture_material_ids" if texture.material_ids is not None else None}, arrays


def texture_fields(spec):
    if spec is None:
        return set()
    if (not isinstance(spec, dict) or set(spec) != {"images", "material_names", "material_ids"}
            or not isinstance(spec["images"], list) or not 1 <= len(spec["images"]) <= 128
            or not isinstance(spec["material_names"], list)):
        raise CodecError("invalid texture array index")
    keys = set()
    for index, image in enumerate(spec["images"]):
        key = f"texture_{index:04d}"
        if (not isinstance(image, dict)
                or set(image) != {"array", "color_space", "wrap", "filter", "name"}
                or image["array"] != key):
            raise CodecError("invalid texture image index")
        keys.add(key)
    if spec["material_ids"] is not None:
        if spec["material_ids"] != "texture_material_ids":
            raise CodecError("invalid texture material index")
        keys.add("texture_material_ids")
    return keys


def read_texture(spec, read):
    if spec is None:
        return None
    texture_fields(spec)
    try:
        images = tuple(TextureImage(read(image["array"]), **{k: v for k, v in image.items() if k != "array"})
                       for image in spec["images"])
        return MeshTexture(images, None if spec["material_ids"] is None else read(spec["material_ids"]),
                           tuple(spec["material_names"]))
    except (ValueError, TypeError) as error:
        raise CodecError(f"invalid texture: {error}") from error
