use crate::protocol::Transport;
use anyhow::{ensure, Context as _, Result};
use rusb::{Context, DeviceHandle, TransferType, UsbContext};
use std::time::Duration;

pub struct Usb {
    handle: DeviceHandle<Context>,
}
impl Usb {
    pub fn open() -> Result<Self> {
        let context = Context::new()?;
        let mut matches = Vec::new();
        for device in context.devices()?.iter() {
            let d = device.device_descriptor()?;
            if (d.vendor_id(), d.product_id()) == (0x054c, 0x04b2) {
                matches.push(device);
            }
        }
        ensure!(
            matches.len() == 1,
            "Expected exactly one active 054c:04b2, found {}",
            matches.len()
        );
        let device = matches.pop().unwrap();
        let handle = device
            .open()
            .context("Open CECH-ZD1J; check the video group/udev rule")?;
        if handle.active_configuration()? != 1 {
            handle.set_active_configuration(1)?;
        }
        handle.set_auto_detach_kernel_driver(true)?;
        handle.claim_interface(0)?;
        // Release interface even when a subsequent validation fails.
        let usb = Self { handle };
        usb.handle.set_alternate_setting(0, 0)?;
        let config = device.active_config_descriptor()?;
        let interface = config
            .interfaces()
            .find(|i| i.number() == 0)
            .context("Missing USB interface 0")?;
        let descriptor = interface
            .descriptors()
            .find(|d| d.setting_number() == 0 && d.class_code() == 0xff)
            .context("Unexpected USB interface")?;
        let mut endpoints: Vec<_> = descriptor
            .endpoint_descriptors()
            .map(|e| (e.address(), e.transfer_type()))
            .collect();
        endpoints.sort_by_key(|e| e.0);
        ensure!(
            endpoints
                == [
                    (0x02, TransferType::Bulk),
                    (0x81, TransferType::Bulk),
                    (0x83, TransferType::Interrupt),
                    (0x84, TransferType::Bulk)
                ],
            "Unexpected USB endpoints: {endpoints:?}"
        );
        Ok(usb)
    }
}
impl Drop for Usb {
    fn drop(&mut self) {
        let _ = self.handle.release_interface(0);
    }
}
impl Transport for Usb {
    fn control(
        &self,
        kind: u8,
        request: u8,
        value: u16,
        index: u16,
        data: &mut [u8],
    ) -> Result<()> {
        let timeout = Duration::from_secs(1);
        let count = if kind & 0x80 != 0 {
            self.handle
                .read_control(kind, request, value, index, data, timeout)?
        } else {
            self.handle
                .write_control(kind, request, value, index, data, timeout)?
        };
        ensure!(
            count == data.len(),
            "Short USB control transfer {count}/{}",
            data.len()
        );
        Ok(())
    }
    fn pipe(&self, id: u8, length: usize) -> Result<Vec<u8>> {
        let mut data = vec![0; length];
        // rusb preserves partial bulk data on timeout. Feed it to the media
        // converter so arbitrary USB boundaries never lose a TS packet.
        let result = if id == 0x83 {
            self.handle
                .read_interrupt(id, &mut data, Duration::from_millis(500))
        } else {
            self.handle
                .read_bulk(id, &mut data, Duration::from_millis(250))
        };
        match result {
            Ok(n) => {
                data.truncate(n);
                Ok(data)
            }
            Err(rusb::Error::Timeout | rusb::Error::Interrupted) => Ok(Vec::new()),
            Err(e) => Err(e.into()),
        }
    }
    fn write_pipe(&self, data: &[u8]) -> Result<()> {
        let count = self.handle.write_bulk(0x02, data, Duration::from_secs(3))?;
        ensure!(
            count == data.len(),
            "Short firmware write {count}/{}",
            data.len()
        );
        Ok(())
    }
}
