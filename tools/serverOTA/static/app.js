/* Dashboard JS - Polling dữ liệu và render UI */

function renderFiles(files) {
  const tbody = document.getElementById("firmwareTable").querySelector("tbody");
  tbody.innerHTML = "";

  if (!files || files.length === 0) {
    tbody.innerHTML =
      '<tr><td colspan="5" class="empty-state">Chưa có firmware. Upload ở mục bên trên.</td></tr>';
    return;
  }

  files.forEach((f) => {
    const row = document.createElement("tr");
    row.innerHTML = `
      <td><a href="/${f.name}" target="_blank">📄 ${f.name}</a></td>
      <td><span class="badge approved" style="font-size:0.85rem;">v${f.version || "?"}</span></td>
      <td class="file-size">${f.size}</td>
      <td><code style="font-size:0.75rem;color:#64748b;">${f.md5 || "-"}</code></td>
      <td class="file-date">${f.time}</td>`;
    tbody.appendChild(row);
  });
}

function formatFlash(kb) {
  if (!kb) return "0 KB";
  return kb >= 1024 ? (kb / 1024).toFixed(0) + " MB" : kb + " KB";
}

function formatBytes(bytes) {
  if (bytes < 1024) return bytes + " B";
  if (bytes < 1024 * 1024) return (bytes / 1024).toFixed(1) + " KB";
  return (bytes / (1024 * 1024)).toFixed(2) + " MB";
}

function renderDevices(devices, version_clients, active_downloads) {
  const tbody = document.getElementById("pendingTable").querySelector("tbody");
  tbody.innerHTML = "";

  const all = {};

  if (devices) {
    Object.entries(devices).forEach(([mac, dev]) => {
      const key = mac.toLowerCase(); // Chìa khóa chính là MAC
      all[key] = {
        mac,
        ip: dev.ip,
        chip: dev.chip,
        cores: dev.cores,
        app_name: dev.app_name,
        app_version: dev.app_version,
        flash_kb: dev.flash_kb,
        ota_status: dev.status || "approved",
        timestamp: dev.timestamp,
        version_checks: 0,
        is_pending_device: true,
      };
    });
  }

  if (version_clients) {
    Object.entries(version_clients).forEach(([ip, info]) => {
      // Tìm xem có thiết bị nào có MAC đã kết nối từ IP này không
      const dev_with_ip = Object.values(all).find((d) => d.ip === ip);
      if (dev_with_ip) {
        dev_with_ip.version_checks = info.count;
      } else {
        // Nếu client chỉ mới check version (chưa gửi MAC) thì dùng IP làm key tạm
        const key = "ip-" + ip;
        all[key] = {
          mac: "-",
          ip,
          chip: "-",
          cores: "?",
          app_name: "-",
          app_version: "-",
          flash_kb: 0,
          ota_status: "none",
          timestamp: info.last_time,
          version_checks: info.count,
        };
      }
    });
  }

  const list = Object.values(all);
  if (list.length === 0) {
    tbody.innerHTML =
      '<tr><td colspan="4" class="empty-state">Chưa có thiết bị nào</td></tr>';
    document.getElementById("pendingAlert").style.display = "none";
    return;
  }

  let hasAlert = false;
  list.forEach((dev) => {
    // Match download progress bằng MAC (primary) hoặc IP (fallback)
    const dl =
      active_downloads &&
      (active_downloads[dev.mac] || active_downloads[dev.ip]);
    if (dev.ota_status === "pending") hasAlert = true;

    // Progress bar
    let progressHtml = "";
    if (dl) {
      progressHtml = `
        <div class="progress-container">
          <div class="progress-bar" style="width:${dl.percent}%"></div>
        </div>
        <span class="small-text">📥 ${dl.percent}% (${dl.speed})</span>`;
    } else if (dev.version_checks > 0) {
      progressHtml = `<span class="small-text">🔍 Checked: ${dev.version_checks}x</span>`;
    }

    // Badge
    let badge = "";
    if (dl)
      badge = '<span class="badge downloading">🔄 Đang tải Firmware</span>';
    else if (dev.ota_status === "approved")
      badge = '<span class="badge approved">✅ Đã cấp phép</span>';
    else if (dev.ota_status === "denied")
      badge = '<span class="badge denied">🛑 Đã Chặn</span>';
    else if (dev.ota_status === "pending")
      badge =
        '<span class="badge pending" style="border: 1px solid var(--orange); box-shadow: 0 0 8px var(--orange-bg);">⚠️ Chờ cấp phép</span>';
    else
      badge =
        '<span class="badge" style="background:#334155;color:#94a3b8;">Mới</span>';

    // Buttons
    let btns = "-";
    if (dev.ota_status === "pending" && !dl) {
      btns = `
        <button class="btn btn-approve" onclick="handleAction('${dev.mac}','approve')" style="margin-right: 4px;">✅ Cấp phép</button>
        <button class="btn btn-deny" onclick="handleAction('${dev.mac}','deny')">🛑 Từ chối</button>`;
    } else if (dev.ota_status === "approved" && !dl) {
      btns = `<button class="btn btn-deny" onclick="handleAction('${dev.mac}','deny')">Chặn</button>`;
    } else if (dev.ota_status === "denied" && !dl) {
      btns = `<button class="btn btn-approve" onclick="handleAction('${dev.mac}','approve')">Mở chặn</button>`;
    }

    // Device info
    let devInfo =
      dev.chip !== "-"
        ? `${dev.chip} (${dev.cores} cores)<br><span class="dev-app">App: ${dev.app_name} v${dev.app_version} | Flash: ${formatFlash(dev.flash_kb)}</span>`
        : '<span class="dev-app">Chưa có thông tin</span>';

    const row = document.createElement("tr");
    row.innerHTML = `
      <td><div class="dev-info"><div class="dev-chip"><code>${dev.mac}</code></div><div class="dev-app">${dev.ip}</div></div></td>
      <td><div class="dev-info">${devInfo}</div></td>
      <td><div style="margin-bottom:4px;">${badge}</div><div class="file-date" style="margin-bottom:4px;">${dev.timestamp}</div>${progressHtml}</td>
      <td>${btns}</td>`;
    tbody.appendChild(row);
  });

  document.getElementById("pendingAlert").style.display = hasAlert
    ? "inline-flex"
    : "none";
}

function renderActiveDownloads(activeDownloads) {
  const section = document.getElementById("activeOtaSection");
  const list = document.getElementById("activeOtaList");

  if (!activeDownloads || Object.keys(activeDownloads).length === 0) {
    section.style.display = "none";
    list.innerHTML = "";
    return;
  }

  section.style.display = "block";
  list.innerHTML = "";

  Object.entries(activeDownloads).forEach(([key, dl]) => {
    const card = document.createElement("div");
    card.className = "card ota-progress-card";
    const downloaded = dl.downloaded ? formatBytes(dl.downloaded) : "0 B";
    const total = dl.total ? formatBytes(dl.total) : "?";
    card.innerHTML = `
      <div class="ota-progress-header">
        <div>
          <span class="ota-progress-label">📥 OTA Update</span>
          <span class="ota-progress-key">${dl.mac || dl.ip || key}</span>
        </div>
        <span class="ota-progress-percent">${dl.percent}%</span>
      </div>
      <div class="progress-container ota-progress-bar-large">
        <div class="progress-bar" style="width:${dl.percent}%"></div>
      </div>
      <div class="ota-progress-footer">
        <span>${downloaded} / ${total}</span>
        <span>⚡ ${dl.speed || "0 B/s"}</span>
      </div>`;
    list.appendChild(card);
  });
}

function fetchApiData() {
  fetch("/api/data")
    .then((r) => r.json())
    .then((data) => {
      document.getElementById("serverAddress").innerText = data.server.address;
      document.getElementById("serverVersion").innerText = data.server.version;
      document.getElementById("checkCount").innerText =
        data.server.version_checks;
      document.getElementById("downloadCount").innerText =
        data.server.downloads;
      renderFiles(data.firmware_files);
      renderActiveDownloads(data.active_downloads);
      renderDevices(data.devices, data.version_clients, data.active_downloads);
    })
    .catch((err) => console.error("Error:", err));
}

function handleAction(mac, action) {
  if (mac === "-") return;
  fetch(`/api/${action}-device`, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ mac }),
  })
    .then((r) => r.json())
    .then((d) => {
      if (d.ok) fetchApiData();
    });
}

// ============================================================
// Upload Firmware
// ============================================================

let selectedFile = null;

document.addEventListener("DOMContentLoaded", function () {
  const fileInput = document.getElementById("firmwareFileInput");
  const zone = document.getElementById("uploadZone");

  fileInput.addEventListener("change", (e) => {
    if (e.target.files.length > 0) handleFileSelected(e.target.files[0]);
  });

  zone.addEventListener("dragover", (e) => {
    e.preventDefault();
    zone.classList.add("drag-over");
  });
  zone.addEventListener("dragleave", (e) => {
    e.preventDefault();
    zone.classList.remove("drag-over");
  });
  zone.addEventListener("drop", (e) => {
    e.preventDefault();
    zone.classList.remove("drag-over");
    if (e.dataTransfer.files.length > 0) {
      const f = e.dataTransfer.files[0];
      f.name.endsWith(".bin")
        ? handleFileSelected(f)
        : alert("Chỉ chấp nhận file .bin!");
    }
  });
});

function handleFileSelected(file) {
  selectedFile = file;
  document.getElementById("uploadFilename").textContent = "📄 " + file.name;
  document.getElementById("uploadFilesize").textContent = formatBytes(
    file.size,
  );
  document.getElementById("uploadForm").style.display = "block";
  document.getElementById("uploadZone").style.display = "none";
  document.getElementById("uploadResult").style.display = "none";
  document.getElementById("uploadProgress").style.display = "none";
}

function cancelUpload() {
  selectedFile = null;
  document.getElementById("uploadForm").style.display = "none";
  document.getElementById("uploadZone").style.display = "flex";
  document.getElementById("firmwareFileInput").value = "";
  document.getElementById("uploadVersion").value = "";
}

function uploadFirmware() {
  if (!selectedFile) return;
  const version = document.getElementById("uploadVersion").value.trim();
  const formData = new FormData();
  formData.append("file", selectedFile);
  formData.append("version", version);

  const btn = document.getElementById("btnUpload");
  btn.disabled = true;
  btn.textContent = "⏳ Uploading...";
  document.getElementById("uploadProgress").style.display = "block";

  const xhr = new XMLHttpRequest();
  xhr.upload.addEventListener("progress", (e) => {
    if (e.lengthComputable) {
      const pct = Math.round((e.loaded / e.total) * 100);
      document.getElementById("uploadProgressBar").style.width = pct + "%";
      document.getElementById("uploadStatus").textContent =
        `${pct}% (${formatBytes(e.loaded)} / ${formatBytes(e.total)})`;
    }
  });

  xhr.addEventListener("load", () => {
    btn.disabled = false;
    btn.textContent = "⬆️ Upload lên Server";
    if (xhr.status === 200) {
      const data = JSON.parse(xhr.responseText);
      if (data.ok) {
        document.getElementById("uploadResult").style.display = "block";
        document.getElementById("uploadResult").innerHTML = `
          <div class="upload-success">✅ Upload OK!<br>
            <span class="small-text">${data.filename} | ${data.size} | MD5: <code>${data.md5}</code> | v${data.version}</span>
          </div>`;
        cancelUpload();
        fetchApiData();
      } else alert("Lỗi: " + (data.reason || "?"));
    } else alert("Upload lỗi! HTTP " + xhr.status);
  });

  xhr.addEventListener("error", () => {
    btn.disabled = false;
    btn.textContent = "⬆️ Upload lên Server";
    alert("Lỗi kết nối!");
  });
  xhr.open("POST", "/api/upload-firmware");
  xhr.send(formData);
}

fetchApiData();
setInterval(fetchApiData, 1500);
