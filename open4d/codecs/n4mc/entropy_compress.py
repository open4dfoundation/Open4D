import heapq
import os


class HuffmanCoding:
    def __init__(self, path):
        self.path = path
        self.heap = []
        self.codes = {}
        self.reverse_mapping = {}

    class HeapNode:
        def __init__(self, byte, freq):
            self.byte = byte  # now handles raw bytes (0-255)
            self.freq = freq
            self.left = None
            self.right = None

        def __lt__(self, other):
            return self.freq < other.freq

        def __eq__(self, other):
            if other is None:
                return False
            if not isinstance(other, HuffmanCoding.HeapNode):
                return False
            return self.freq == other.freq

    # functions for compression:

    def make_frequency_dict(self, data):
        frequency = {}
        for byte in data:  # data is a bytes object
            if byte not in frequency:
                frequency[byte] = 0
            frequency[byte] += 1
        return frequency

    def make_heap(self, frequency):
        for key in frequency:
            node = self.HeapNode(key, frequency[key])
            heapq.heappush(self.heap, node)

    def merge_nodes(self):
        while len(self.heap) > 1:
            node1 = heapq.heappop(self.heap)
            node2 = heapq.heappop(self.heap)

            merged = self.HeapNode(None, node1.freq + node2.freq)
            merged.left = node1
            merged.right = node2

            heapq.heappush(self.heap, merged)

    def make_codes_helper(self, root, current_code):
        if root is None:
            return

        if root.byte is not None:  # leaf node
            self.codes[root.byte] = current_code
            self.reverse_mapping[current_code] = root.byte
            return

        self.make_codes_helper(root.left, current_code + "0")
        self.make_codes_helper(root.right, current_code + "1")

    def make_codes(self):
        root = heapq.heappop(self.heap)
        current_code = ""
        self.make_codes_helper(root, current_code)

    def get_encoded_text(self, data):
        encoded_text = ""
        for byte in data:
            encoded_text += self.codes[byte]
        return encoded_text

    def pad_encoded_text(self, encoded_text):
        extra_padding = 8 - len(encoded_text) % 8
        for i in range(extra_padding):
            encoded_text += "0"

        padded_info = "{0:08b}".format(extra_padding)
        encoded_text = padded_info + encoded_text
        return encoded_text

    def get_byte_array(self, padded_encoded_text):
        if len(padded_encoded_text) % 8 != 0:
            print("Encoded text not padded properly")
            exit(0)

        b = bytearray()
        for i in range(0, len(padded_encoded_text), 8):
            byte = padded_encoded_text[i:i + 8]
            b.append(int(byte, 2))
        return b

    def compress(self):
        filename, _ = os.path.splitext(self.path)
        output_path = filename + ".bin"

        with open(self.path, 'rb') as file, open(output_path, 'wb') as output:
            data = file.read()

            frequency = self.make_frequency_dict(data)
            self.make_heap(frequency)
            self.merge_nodes()
            self.make_codes()

            encoded_text = self.get_encoded_text(data)
            padded_encoded_text = self.pad_encoded_text(encoded_text)

            b = self.get_byte_array(padded_encoded_text)
            output.write(bytes(b))

        print("Compressed")
        return output_path

    # functions for decompression:

    def remove_padding(self, padded_encoded_text):
        padded_info = padded_encoded_text[:8]
        extra_padding = int(padded_info, 2)

        padded_encoded_text = padded_encoded_text[8:]  # remove padding info
        encoded_text = padded_encoded_text[:-1 * extra_padding]

        return encoded_text

    def decode_text(self, encoded_text):
        current_code = ""
        decoded_bytes = bytearray()

        for bit in encoded_text:
            current_code += bit
            if current_code in self.reverse_mapping:
                byte = self.reverse_mapping[current_code]
                decoded_bytes.append(byte)
                current_code = ""

        return bytes(decoded_bytes)

    def decompress(self, input_path):
        filename, _ = os.path.splitext(self.path)
        output_path = filename + "_decompressed" + ".pt"

        with open(input_path, 'rb') as file, open(output_path, 'wb') as output:
            bit_string = ""

            byte = file.read(1)
            while len(byte) > 0:
                byte = ord(byte)
                bits = bin(byte)[2:].rjust(8, '0')
                bit_string += bits
                byte = file.read(1)

            encoded_text = self.remove_padding(bit_string)

            decompressed_data = self.decode_text(encoded_text)

            output.write(decompressed_data)

        print("Decompressed")
        return output_path


# Example usage
path = "/media/frozzzen/DataDrive/ChromeDownloads/Dancer_dataset/C4/TSDF/log/dancer_100/checkpoint_0400/decoder.pt"

h = HuffmanCoding(path)

output_path = h.compress()
print("Compressed file path: " + output_path)

decom_path = h.decompress(output_path)
print("Decompressed file path: " + decom_path)
