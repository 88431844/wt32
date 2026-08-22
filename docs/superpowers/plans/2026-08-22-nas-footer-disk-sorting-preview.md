# NAS Footer And Disk Sorting Preview Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Produce a 480 x 320 interactive browser preview with a shorter NAS footer and sortable disk-list headers.

**Architecture:** Keep the existing single-file HTML preview. CSS owns the device-accurate geometry, while a small client-side sort state derives a sorted copy of the complete sample disk collection before slicing the current page.

**Tech Stack:** HTML, CSS, browser JavaScript, Codex in-app Browser

---

### Task 1: Shorten And Recenter The Footer

**Files:**
- Modify: `.superpowers/brainstorm/48921-1787383837/content/implementation-final-v10.html`

- [ ] **Step 1: Record the expected geometry markers**

Use these exact preview values as the visual contract:

```css
.rows { height:236px; }
.footer { top:236px; height:52px; }
.item { height:42px; }
.network { height:48px; grid-template-rows:24px 24px; }
```

- [ ] **Step 2: Update the CSS geometry**

Keep the seven existing footer columns and 14 px font. Center each network row's complete icon-and-value group with a nested flex span:

```css
.network{height:48px;display:grid;grid-template-rows:24px 24px;place-items:center;white-space:nowrap;font-size:14px}
.network>span{width:100%;display:flex;align-items:center;justify-content:center;gap:4px}
```

- [ ] **Step 3: Verify the file markers**

Run:

```bash
rg -n "top:236px|height:52px|grid-template-rows:24px 24px|justify-content:center" .superpowers/brainstorm/48921-1787383837/content/implementation-final-v10.html
```

Expected: all four footer markers are present.

### Task 2: Add Sortable Disk Headers

**Files:**
- Modify: `.superpowers/brainstorm/48921-1787383837/content/implementation-final-v10.html`

- [ ] **Step 1: Add sortable header buttons**

Remove the disk-panel title and replace the passive labels with buttons that do not bubble to the disk-panel return handler:

```html
<div class="disk-head">
  <button data-sort="id" onclick="sortDisks('id',event)">硬盘 <span></span></button>
  <button data-sort="model" onclick="sortDisks('model',event)">型号 <span></span></button>
  <button data-sort="temp" onclick="sortDisks('temp',event)">温度 <span></span></button>
</div>
```

- [ ] **Step 2: Add full-collection sorting**

Use stable source indices and natural ID ordering. Invalid temperatures remain last in either direction:

```js
let diskSort={key:'id',direction:1};
const natural=new Intl.Collator(undefined,{numeric:true,sensitivity:'base'});
function sortedDisks(){
  return disks.map((disk,index)=>({disk,index})).sort((a,b)=>{
    const av=a.disk[diskSort.key],bv=b.disk[diskSort.key];
    if(diskSort.key==='temp'&&(av==null||bv==null)) return av==null?(bv==null?a.index-b.index:1):-1;
    const compared=diskSort.key==='temp'?av-bv:natural.compare(av||'',bv||'');
    return compared===0?a.index-b.index:compared*diskSort.direction;
  }).map(entry=>entry.disk);
}
function sortDisks(key,event){
  event.stopPropagation();
  diskSort=key===diskSort.key?{key,direction:-diskSort.direction}:{key,direction:1};
  dp=0;
  renderDisks();
}
```

- [ ] **Step 3: Render from the sorted collection and update indicators**

```js
function renderDisks(){
  const ordered=sortedDisks();
  document.getElementById('diskRows').innerHTML=ordered.slice(dp,dp+4).map(renderDisk).join('');
  document.querySelectorAll('[data-sort]').forEach(button=>{
    button.querySelector('span').textContent=button.dataset.sort===diskSort.key?(diskSort.direction>0?'↑':'↓'):'';
  });
}
```

- [ ] **Step 4: Keep pager controls visible and disable unavailable directions**

Give the pager buttons stable identifiers and update their disabled state from the sorted collection:

```js
document.getElementById('diskPrev').disabled=dp===0;
document.getElementById('diskNext').disabled=dp+4>=ordered.length;
```

Use a dimmed disabled style instead of hiding either button:

```css
.pager button:disabled{opacity:.32;color:var(--muted)}
```

- [ ] **Step 5: Verify source-level behavior markers**

Run:

```bash
rg -n "Intl.Collator|sortedDisks|sortDisks|event.stopPropagation|dp=0|data-sort|diskPrev|diskNext|button:disabled" .superpowers/brainstorm/48921-1787383837/content/implementation-final-v10.html
```

Expected: natural sorting, event isolation, page reset, and all three controls are present.

### Task 3: Browser Preview QA

**Files:**
- Verify: `.superpowers/brainstorm/48921-1787383837/content/implementation-final-v10.html`
- Create outside repository: `/private/tmp/nas-footer-sort-preview.png`

- [ ] **Step 1: Open the preview at 480 x 320**

Open `http://localhost:63911/implementation-final-v10.html`, set the viewport to 480 x 320, and confirm the page title is `NAS/PVE Monitor Preview`.

- [ ] **Step 2: Verify footer layout**

Confirm the footer occupies y=236 through y=288 within the page, network rows do not overlap, and both icon-and-value groups are horizontally centered.

- [ ] **Step 3: Exercise disk sorting**

Click a pool row, then verify these sequences:

```text
default ID: 硬盘 1, 硬盘 2, 硬盘 3, 硬盘 4
ID descending: 硬盘 10, 硬盘 9, 硬盘 8, 硬盘 7
model ascending: Seagate IronWolf ...
temperature descending: highest valid temperatures first
```

Confirm pager clicks stay in the disk view and a disk-row click returns to pools.
Confirm the first page shows a dimmed up button, the middle page enables both buttons, and the final page shows a dimmed down button.

- [ ] **Step 4: Check rendering health and capture evidence**

Confirm the DOM is nonblank, no framework overlay exists, console warnings/errors are empty, and no text clips or wraps. Save the final screenshot to `/private/tmp/nas-footer-sort-preview.png` and leave the preview visible.
