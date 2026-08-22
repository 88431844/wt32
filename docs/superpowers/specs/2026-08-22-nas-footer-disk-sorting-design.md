# NAS Footer And Disk Sorting Design

## Goal

Reduce the visual weight of the NAS footer while keeping its data readable, and add sortable disk-list headers that operate on the complete interface response before pagination.

## Footer Layout

- Reduce the footer height from 62 px to 52 px.
- Move the footer top edge down by 10 px so it still ends at the bottom of the 288 px page.
- Keep the existing 14 px value font and current horizontal column allocation.
- Vertically center IP, uptime, CPU, memory, and temperature in the shorter footer.
- Keep network traffic on two rows. Each row receives half of the available height.
- Center each network row's icon-and-value group independently within the network column.
- Preserve enough width for long rates supported by the existing compact unit formatter.

## Disk Header Controls

- Replace the passive `硬盘`, `型号`, and `温度` labels with compact sortable header buttons.
- Show the active column's direction with an up or down icon. Inactive columns do not show a direction icon.
- The initial order is disk identifier ascending.
- Clicking the active column reverses its direction.
- Clicking another column selects that column and starts in ascending order.

## Sorting Rules

- Disk identifiers use natural numeric ordering, so `disk 10` follows `disk 9` instead of `disk 1`.
- Models use case-insensitive text ordering.
- Temperatures use numeric ordering.
- Missing identifiers or models sort after populated values.
- Disks without a valid temperature always remain after disks with a valid temperature, in both directions.
- Sorting is applied to the full dynamic disk collection before the four-row page is selected.
- Changing the sort resets the list to page one.
- Sorting state remains local to the current UI session and is not written to NVS.

## Interaction Boundaries

- Header-button clicks only change sorting and must not return to the storage-pool list.
- Pager-button clicks only change pages and must not return to the storage-pool list.
- Clicking the remaining disk-list area returns to the storage-pool list, preserving the existing behavior.

## Preview And Verification

- Update the existing 480 x 320 browser preview before changing firmware behavior.
- Preview the footer with long IP, three-digit percentages/temperature, and long upload/download values.
- Exercise ascending and descending sorting for all three columns across ten sample disks.
- Confirm page one changes after sorting and pagination follows the complete sorted collection.
- Confirm no clipping, overlap, wrapping, or relevant console errors.
- Do not flash the device until the preview is approved.
