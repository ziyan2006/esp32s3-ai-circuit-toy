# ESP-IDF I2C master patch

This component is a project-local copy of the ESP-IDF v5.5.3 `esp_driver_i2c`
component. The behavioral changes are limited to receive-state cleanup in
`i2c_master.c`: every synchronous transaction clears `contains_read` before
programming the hardware, probes clear the same state before setting up their
START/STOP-only transaction, and the ISR clears all pending receive state when
the current transaction reports NACK, timeout, or arbitration loss. The ISR
also refuses to read the RX FIFO if the transaction or destination buffer is
NULL and reports the transaction as failed instead of dereferencing it.

On ESP32-P4, a read that ends in a NACK or timeout could leave that flag set.
The same interrupt can contain both failure and completion/end bits, so merely
clearing the flag at the start of ordinary device transactions is insufficient:
a probe bypasses that setup path and can enter the receive ISR with operations
that have no receive buffer. That causes a NULL-pointer store in
`i2c_ll_read_rxfifo`; this is the failure captured during long play testing.

Keeping the component copy in the project makes the workaround reproducible
without modifying the globally installed ESP-IDF tree. It can be removed once
the upstream driver includes the equivalent fix.
