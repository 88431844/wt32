# PVE Virtual Machine vCPU List Design

## Goal

Change the PVE virtual machine list so its CPU column reports each guest's
configured virtual CPU count rather than current CPU utilization.

## Scope

- Change the virtual machine list header from `CPU` to `vcpu`.
- Render `pve_guest_t.cpu_cores` as a plain decimal number, such as `4` or `8`.
- Render `--` when the reported virtual CPU count is zero or unavailable.
- Keep the existing PVE guest detail CPU field unchanged; it continues to show
  CPU utilization together with the core count.
- Do not change PVE API requests or the snapshot model. The provider already
  maps the PVE `maxcpu` value into `pve_guest_t.cpu_cores`.

## Layout

Keep the current list column boundaries because the existing 48-pixel CPU
column is sufficient for the `vcpu` header and expected numeric values. Give
the header label enough width within that column to avoid clipping.

If font rendering proves that 48 pixels is insufficient, expand the vCPU
column by eight pixels and take those pixels from the virtual machine column.
The IP and memory column widths must remain unchanged.

## Rendering Behavior

For each visible PVE guest row:

- `cpu_cores > 0`: render the unsigned integer without a suffix.
- `cpu_cores == 0`: render `--`.

The list must no longer format or display `cpu_percent`.

## Verification

Update the dashboard UI contract test first and confirm it fails because the
old `CPU` header and percent formatter remain. Then implement the minimal UI
change and verify:

- the focused dashboard UI contract passes;
- the full repository test suite passes;
- the ESP-IDF firmware build succeeds.

