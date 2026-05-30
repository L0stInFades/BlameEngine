//! Classic binary search over the map — player code written in modern Rust (edition 2024,
//! `no_std`), compiled to wasm32 and run inside the sandbox against the live headless world
//! (ADR-0011).
//!
//! The guest senses every BEACON entity through the capability-gated Game API. The Game API
//! returns matches in ascending id order and the world lays beacons out left-to-right, so the
//! sensed x-coordinates form a sorted array. The guest builds that array in its own linear memory
//! and uses the real Rust standard library — `core::slice::binary_search` — to locate the beacon
//! at the target x passed in `run`. Returns the beacon index, or -1 if no beacon sits there.
//!
//! Speaks the identical Game API ABI (`env.host_call`) as the C++ guest and the NBVM guests.

#![no_std]
#![allow(static_mut_refs)] // the sandbox runs one guest, single-threaded; statics are our arena

use core::panic::PanicInfo;

#[panic_handler]
fn panic(_: &PanicInfo) -> ! {
    loop {}
}

#[link(wasm_import_module = "env")]
unsafe extern "C" {
    fn host_call(call_id: i32, a_off: i32, a_len: i32, r_off: i32, r_len: i32) -> i32;
}

const CALL_GET_POSITION: i32 = 5;
const CALL_QUERY_BY_TAG: i32 = 6;
const TAG_BEACON: u32 = 3;
const MAX_BEACONS: usize = 128;

static mut QARG: [u8; 4] = [0; 4]; // QueryByTagArgs { tag: u32 }
static mut LISTBUF: [u8; 8 + 8 * MAX_BEACONS] = [0; 8 + 8 * MAX_BEACONS]; // header + EntityId[]
static mut EARG: [u8; 8] = [0; 8]; // EntityArg { entity: u64 }
static mut POSRET: [u8; 12] = [0; 12]; // PositionResult { Vec3 }
static mut XS: [i32; MAX_BEACONS] = [0; MAX_BEACONS];

#[inline]
fn off<T>(p: *const T) -> i32 {
    p as usize as i32
}

#[unsafe(no_mangle)]
pub extern "C" fn run(target: i32) -> i32 {
    unsafe {
        // 1. Sense every beacon. The result is a [header | EntityId...] blob, ids ascending.
        QARG.copy_from_slice(&TAG_BEACON.to_le_bytes());
        host_call(
            CALL_QUERY_BY_TAG,
            off(QARG.as_ptr()),
            QARG.len() as i32,
            off(LISTBUF.as_ptr()),
            LISTBUF.len() as i32,
        );
        let count = u32::from_le_bytes([LISTBUF[0], LISTBUF[1], LISTBUF[2], LISTBUF[3]]) as usize;
        let n = if count < MAX_BEACONS {
            count
        } else {
            MAX_BEACONS
        };

        // 2. Read each beacon's x into XS (ascending, since ids are ascending and the world places
        //    beacons left-to-right) — a sorted "map index" built from real sensing.
        for i in 0..n {
            let base = 8 + i * 8;
            let mut idb = [0u8; 8];
            idb.copy_from_slice(&LISTBUF[base..base + 8]);
            let id = u64::from_le_bytes(idb);
            EARG.copy_from_slice(&id.to_le_bytes());
            host_call(
                CALL_GET_POSITION,
                off(EARG.as_ptr()),
                8,
                off(POSRET.as_ptr()),
                12,
            );
            let xf = f32::from_le_bytes([POSRET[0], POSRET[1], POSRET[2], POSRET[3]]);
            XS[i] = xf as i32;
        }

        // 3. Binary-search the sorted map with the standard library.
        match XS[..n].binary_search(&target) {
            Ok(idx) => idx as i32,
            Err(_) => -1,
        }
    }
}
