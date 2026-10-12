import XCTest
@testable import DeskPetHardware

final class WireTests: XCTestCase {
    func testFragmentedKoreanAndBinary() throws {
        let message = WireMessage(meta: ["version": .int(1), "text": .string("듣고 있어요")], data: Data([0, 255, 1, 128]))
        let wire = try message.encoded()
        var decoder = WireDecoder(), result: [WireMessage] = []
        for byte in wire { result += try decoder.feed(Data([byte])) }
        XCTAssertEqual(result.count, 1)
        XCTAssertEqual(result[0].meta, message.meta)
        XCTAssertEqual(result[0].data, message.data)
    }
    func testRejectsOversizeBeforeAllocation() {
        var decoder = WireDecoder()
        XCTAssertThrowsError(try decoder.feed(Data([0, 0, 64, 1, 0, 0, 0, 0])))
    }
    func testMultipleFramesAndInt64Timestamp() throws {
        let message = WireMessage(meta: ["version": .int(1), "timestamp": .int(1_790_000_000_000_000_001)])
        var decoder = WireDecoder()
        let result = try decoder.feed(message.encoded() + message.encoded())
        XCTAssertEqual(result.count, 2)
        XCTAssertEqual(result[1].meta["timestamp"]?.integer, 1_790_000_000_000_000_001)
    }
    func testInvalidPCMRejected() {
        XCTAssertThrowsError(try PCMChunk(data: Data([1]), sampleRate: 16000).validate())
        XCTAssertThrowsError(try PCMChunk(data: Data([1, 2]), sampleRate: 96000).validate())
        XCTAssertThrowsError(try PCMChunk(data: Data([1, 2]), channels: 2).validate())
    }
}
