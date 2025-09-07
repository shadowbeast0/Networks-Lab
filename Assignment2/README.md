# Networks-Lab Assignment 2 — Stop-and-Wait, Go-Back-N, Selective Repeat (Windows)

## Prerequisites
- Windows + Visual Studio (**Developer Command Prompt for VS**)
- All sources in: `C:\01_Arjeesh_Drive\CN\Assignment2_try2`
- Generate `data.txt` with `make_data.exe` before running
- **Start the SENDER first** (it listens), then start the RECEIVER (it connects)

---

## Build (Developer Command Prompt)
    cd /d C:\01_Arjeesh_Drive\CN\Assignment2_try2
    cl /EHsc /O2 /std:c++17 make_data.cpp         /Fe:make_data.exe
    cl /EHsc /O2 /std:c++17 stopwait_sender.cpp   /Fe:stopwait_sender.exe
    cl /EHsc /O2 /std:c++17 stopwait_receiver.cpp /Fe:stopwait_receiver.exe
    cl /EHsc /O2 /std:c++17 gobackn_sender.cpp    /Fe:gobackn_sender.exe
    cl /EHsc /O2 /std:c++17 gobackn_receiver.cpp  /Fe:gobackn_receiver.exe
    cl /EHsc /O2 /std:c++17 sr_sender.cpp         /Fe:sr_sender.exe
    cl /EHsc /O2 /std:c++17 sr_receiver.cpp       /Fe:sr_receiver.exe

## Data (create data.txt)
    make_data.exe

## Program Arguments
- stopwait_sender.exe `<p_err>` `<max_delay_ms>`
- stopwait_receiver.exe `<p_err>` `<max_delay_ms>`
- gobackn_sender.exe `<N>` `<p_err>` `<max_delay_ms>`
- gobackn_receiver.exe `<p_err>` `<max_delay_ms>`
- sr_sender.exe `<N>` `<p_err>` `<max_delay_ms>`
- sr_receiver.exe `<N>` `<p_err>` `<max_delay_ms>`

Notes:
- `p_err` is **per-bit** error probability on that process’s path  
  (sender → data frames, receiver → ACK/NAK)
- `max_delay_ms` is uniform random delay upper bound `[0..max_delay_ms]`
- **Run order: SENDER first, RECEIVER second**

---

## Test Cases + Expected Output Patterns (SENDER in Terminal A, RECEIVER in Terminal B)

### Stop-and-Wait

TC1 — Baseline (no error/loss)
- Terminal A: `stopwait_sender.exe 0 0`
- Terminal B: `stopwait_receiver.exe 0 0`
- Expected:
  - Receiver: only `CRC=OK`
  - Sender: `ACK <seq>` for each frame; **no timeouts**

TC2 — Delay only (observe idle/waiting time)
- Terminal A: `stopwait_sender.exe 0 120`
- Terminal B: `stopwait_receiver.exe 0 120`
- Expected:
  - Receiver: `CRC=OK`
  - Sender: occasional `Timeout; retransmitting seq=<n>`; **eventual progress**

TC3 — Mixed (moderate bit errors + delay)
- Terminal A: `stopwait_sender.exe 0.0005 100`
- Terminal B: `stopwait_receiver.exe 0.0005 100`
- Expected:
  - Receiver: mix of `CRC=OK` / `CRC=BAD`
  - Sender: retries same seq until ACKed; **steady but slower** progress

---

### Go-Back-N

TC1 — Baseline (no error/loss), N=4
- Terminal A: `gobackn_sender.exe 4 0 0`
- Terminal B: `gobackn_receiver.exe 0 0`
- Expected:
  - Receiver: `seq=0 CRC=OK expected=0`, then `Sent cumulative ACK=1`, etc.
  - **No** `CRC=BAD`, **no** timeouts

TC2 — Delay only, N=4 (window timeouts)
- Terminal A: `gobackn_sender.exe 4 0 150`
- Terminal B: `gobackn_receiver.exe 0 150`
- Expected:
  - Sender: occasional `TIMEOUT, resending window [b,e)`
  - Receiver: mostly `CRC=OK`; higher seq before missing one ⇒  
    `out-of-order or corrupted -> discard`; cumulative ACK **holds** until gap closes

TC3 — Mixed, N=8 (errors + delay)
- Terminal A: `gobackn_sender.exe 8 0.0005 100`
- Terminal B: `gobackn_receiver.exe 0.0005 100`
- Expected:
  - Receiver: intermittent `CRC=BAD`, frequent `out-of-order or corrupted -> discard` until missing seq received; then `Sent cumulative ACK=<k>`
  - Sender: periodic timeouts resending the current window; **progress in bursts**

Typical receiver snippet:
    [GBN RECV] seq=5 CRC=BAD expected=5
      out-of-order or corrupted -> discard
    [GBN RECV] seq=7 CRC=OK expected=5
      out-of-order or corrupted -> discard
    [GBN RECV] seq=5 CRC=OK expected=5
    [GBN RECV] Sent cumulative ACK=6

---

### Selective Repeat

TC1 — Baseline (no error/loss), N=4
- Terminal A: `sr_sender.exe 4 0 0`
- Terminal B: `sr_receiver.exe 4 0 0`
- Expected:
  - Receiver: `-> ACK <seq>` for each frame
  - **No** NAKs, **no** timeouts

TC2 — Delay only, N=5 (selective retransmissions)
- Terminal A: `sr_sender.exe 5 0 120`
- Terminal B: `sr_receiver.exe 5 0 120`
- Expected:
  - Sender: occasional `Timeout seq=<n> -> retransmit`
  - Receiver: accepts in-window out-of-order frames; base **advances** as the gap fills

TC3 — Mixed, N=6 (errors + delay)
- Terminal A: `sr_sender.exe 6 0.0005 100`
- Terminal B: `sr_receiver.exe 6 0.0005 100`
- Expected:
  - Receiver: `CRC=BAD` ⇒ `-> NAK <base>`; buffers valid out-of-order frames; **re-ACKs valid duplicates (`seq < base`)**; base increments once gap is filled
  - Sender: **targeted** retransmissions of missing seqs; steady progress resumes after each NAK/timeout

Typical receiver snippet:
    [SR RECV] seq=1 CRC=BAD base=1
      -> NAK 1
    [SR RECV] seq=2 CRC=OK base=1
      -> ACK 2
    [SR RECV] seq=1 CRC=OK base=1
      -> ACK 1

---

## Troubleshooting
- `CRC=BAD` with `p_err=0` ⇒ rebuild both EXEs and rerun baseline
- GBN “Incomplete frame.” ⇒ use the updated receiver that reads exact frame size with adaptive tail timeout; rebuild if needed
- SR stalls ⇒ use the updated SR receiver that **re-ACKs valid duplicates**; rebuild if behavior differs
- Ensure `data.txt` exists (`make_data.exe`)
- **Order matters:** **start SENDER first**, then start RECEIVER
