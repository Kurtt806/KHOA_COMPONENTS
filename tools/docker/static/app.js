function renderFiles(files) {
  const fwTable = document
    .getElementById("firmwareTable")
    .querySelector("tbody");
  fwTable.innerHTML = "";

  if (!files || files.length === 0) {
    fwTable.innerHTML =
      '<tr><td colspan="5" class="empty-state">Chưa có file firmware. Hãy upload ở mục bên trên.</td></tr>';
    return;
  }

  files.forEach((f) => {
    const row = document.createElement("tr");
    row.innerHTML = `
            <td><a href="/${f.name}" target="_blank">📄 ${f.name}</a></td>
            <td><span class="badge approved" style="font-size: 0.85rem;">v${f.version || "?"}</span></td>
            <td class="file-size">${f.size}</td>
            <td><code style="font-size: 0.75rem; color: #64748b;">${f.md5 || "-"}</code></td>
            <td class="file-date">${f.time}</td>
        `;
    fwTable.appendChild(row);
  });
}

function formatFlash(kb) {
  if (!kb) return "0 KB";
  if (kb >= 1024) return (kb / 1024).toFixed(0) + " MB";
  return kb + " KB";
}

// Chuyển bytes sang đơn vị dễ đọc
function formatBytes(bytes) {
  if (bytes < 1024) return bytes + " B";
  if (bytes < 1024 * 1024) return (bytes / 1024).toFixed(1) + " KB";
  return (bytes / (1024 * 1024)).toFixed(2) + " MB";
}

function renderDevices(devices, versionClients, activeDownloads) {
  const tableBody = document
    .getElementById("pendingTable")
    .querySelector("tbody");
  tableBody.innerHTML = "";

  let hasAlert = false;

  const allDevicesMap = {};

  if (devices) {
    Object.entries(devices).forEach(([mac, dev]) => {
      allDevicesMap[dev.ip] = {
        mac: mac,
        ip: dev.ip,
        chip: dev.chip,
        cores: dev.cores,
        app_name: dev.app_name,
        app_version: dev.app_version,
        flash_kb: dev.flash_kb,
        ota_status: dev.status,
        type: dev.type || "verified",
        timestamp: dev.timestamp,
        version_checks: 0,
      };
    });
  }

  if (versionClients) {
    Object.entries(versionClients).forEach(([ip, info]) => {
      if (allDevicesMap[ip]) {
        allDevicesMap[ip].version_checks = info.count;
      } else {
        allDevicesMap[ip] = {
          mac: "-",
          ip: ip,
          chip: "-",
          cores: "?",
          app_name: "-",
          app_version: "-",
          flash_kb: 0,
          ota_status: "none",
          type: "none",
          timestamp: info.last_time,
          version_checks: info.count,
        };
      }
    });
  }

  const deviceAnysize = Object.values(allDevicesMap);

  if (deviceAnysize.length === 0) {
    tableBody.innerHTML =
      '<tr><td colspan="4" class="empty-state">Chưa đăng ký thiết bị nào</td></tr>';
    document.getElementById("pendingAlert").style.display = "none";
    return;
  }

  deviceAnysize.sort((a, b) => {
    if (a.ota_status === "pending" && b.ota_status !== "pending") return -1;
    if (a.ota_status !== "pending" && b.ota_status === "pending") return 1;
    return 0;
  });

  deviceAnysize.forEach((dev) => {
    const isDownloading = activeDownloads && activeDownloads[dev.ip];
    if (dev.ota_status === "pending") hasAlert = true;

    const row = document.createElement("tr");

    let progressHtml = "";
    if (isDownloading) {
      const dl = activeDownloads[dev.ip];
      progressHtml = `
                 <div class="progress-container">
                     <div class="progress-bar" style="width: ${dl.percent}%"></div>
                 </div>
                 <span class="small-text">📥 Tải: ${dl.percent}% (${dl.speed})</span>
             `;
    } else if (dev.version_checks > 0) {
      progressHtml = `<span class="small-text">🔍 Đã kiểm tra version: ${dev.version_checks} lần</span>`;
    }

    let badgeHtml = "";
    if (isDownloading)
      badgeHtml = '<span class="badge downloading">🔄 Đang cập nhật OTA</span>';
    else if (dev.ota_status === "pending" && dev.type === "no_key")
      badgeHtml = '<span class="badge nokey">🔑 Chưa kích hoạt</span>';
    else if (dev.ota_status === "pending")
      badgeHtml = '<span class="badge pending">⏳ Chờ duyệt</span>';
    else if (dev.ota_status === "approved")
      badgeHtml = '<span class="badge approved">✅ Đã duyệt</span>';
    else if (dev.ota_status === "denied")
      badgeHtml = '<span class="badge denied">❌ Từ chối</span>';
    else
      badgeHtml =
        '<span class="badge" style="background: #334155; color: #94a3b8;">Hiển thị Version</span>';

    let btnHtml = "-";
    if (dev.ota_status === "pending" && !isDownloading) {
      if (dev.type === "no_key") {
        btnHtml = `
                    <button class="btn btn-activate" onclick="handleAction('${dev.mac}', 'approve')">Kích hoạt</button>
                    <button class="btn btn-deny" onclick="handleAction('${dev.mac}', 'deny')">Từ chối</button>
                `;
      } else {
        btnHtml = `
                    <button class="btn btn-approve" onclick="handleAction('${dev.mac}', 'approve')">Cho phép</button>
                    <button class="btn btn-deny" onclick="handleAction('${dev.mac}', 'deny')">Từ chối</button>
                `;
      }
    }

    let flashInfo = dev.flash_kb > 0 ? formatFlash(dev.flash_kb) : "?";
    let deviceInfo =
      dev.chip !== "-"
        ? `${dev.chip} (${dev.cores} cores)<br><span class="dev-app">App: ${dev.app_name} v${dev.app_version} | Flash: ${flashInfo}</span>`
        : '<span class="dev-app">Chưa có thông tin phần cứng</span>';

    row.innerHTML = `
            <td>
                <div class="dev-info">
                    <div class="dev-chip"><code>${dev.mac}</code></div>
                    <div class="dev-app">${dev.ip}</div>
                </div>
            </td>
            <td>
                <div class="dev-info">
                    ${deviceInfo}
                </div>
            </td>
            <td>
                <div style="margin-bottom: 4px;">${badgeHtml}</div>
                <div class="file-date" style="margin-bottom: 4px;">Lần cuối: ${dev.timestamp}</div>
                ${progressHtml}
            </td>
            <td>${btnHtml}</td>
        `;
    tableBody.appendChild(row);
  });

  document.getElementById("pendingAlert").style.display = hasAlert
    ? "inline-flex"
    : "none";
}

function fetchApiData() {
  fetch("/api/data")
    .then((res) => res.json())
    .then((data) => {
      document.getElementById("serverAddress").innerText = data.server.address;
      document.getElementById("serverVersion").innerText = data.server.version;
      document.getElementById("checkCount").innerText =
        data.server.version_checks;
      document.getElementById("downloadCount").innerText =
        data.server.downloads;

      renderFiles(data.firmware_files);
      renderDevices(data.devices, data.version_clients, data.active_downloads);
    })
    .catch((err) => console.error("Error fetching data:", err));
}

function handleAction(mac, action) {
  if (mac === "-") return;
  fetch(`/${action}-device`, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ mac: mac }),
  })
    .then((res) => res.json())
    .then((data) => {
      if (data.ok) fetchApiData();
      else alert("Error updating device status!");
    });
}

// ============================================================
// Upload Firmware
// ============================================================

let selectedFile = null;

// Xử lý chọn file qua input
document.addEventListener("DOMContentLoaded", function () {
  const fileInput = document.getElementById("firmwareFileInput");
  const uploadZone = document.getElementById("uploadZone");

  fileInput.addEventListener("change", function (e) {
    if (e.target.files.length > 0) {
      handleFileSelected(e.target.files[0]);
    }
  });

  // Drag and Drop
  uploadZone.addEventListener("dragover", function (e) {
    e.preventDefault();
    uploadZone.classList.add("drag-over");
  });

  uploadZone.addEventListener("dragleave", function (e) {
    e.preventDefault();
    uploadZone.classList.remove("drag-over");
  });

  uploadZone.addEventListener("drop", function (e) {
    e.preventDefault();
    uploadZone.classList.remove("drag-over");
    if (e.dataTransfer.files.length > 0) {
      const file = e.dataTransfer.files[0];
      if (file.name.endsWith(".bin")) {
        handleFileSelected(file);
      } else {
        alert("Chỉ chấp nhận file .bin!");
      }
    }
  });
});

// Hiển thị thông tin file đã chọn
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

// Hủy chọn file
function cancelUpload() {
  selectedFile = null;
  document.getElementById("uploadForm").style.display = "none";
  document.getElementById("uploadZone").style.display = "flex";
  document.getElementById("firmwareFileInput").value = "";
  document.getElementById("uploadVersion").value = "";
}

// Upload firmware lên server qua XHR (có progress)
function uploadFirmware() {
  if (!selectedFile) return;

  const version = document.getElementById("uploadVersion").value.trim();
  const formData = new FormData();
  formData.append("file", selectedFile);
  formData.append("version", version);

  const btnUpload = document.getElementById("btnUpload");
  btnUpload.disabled = true;
  btnUpload.textContent = "⏳ Đang upload...";

  document.getElementById("uploadProgress").style.display = "block";

  const xhr = new XMLHttpRequest();

  // Theo dõi tiến trình upload
  xhr.upload.addEventListener("progress", function (e) {
    if (e.lengthComputable) {
      const percent = Math.round((e.loaded / e.total) * 100);
      document.getElementById("uploadProgressBar").style.width = percent + "%";
      document.getElementById("uploadStatus").textContent =
        `Đang tải lên: ${percent}% (${formatBytes(e.loaded)} / ${formatBytes(e.total)})`;
    }
  });

  xhr.addEventListener("load", function () {
    btnUpload.disabled = false;
    btnUpload.textContent = "⬆️ Upload lên Server";

    if (xhr.status === 200) {
      const data = JSON.parse(xhr.responseText);
      if (data.ok) {
        document.getElementById("uploadResult").style.display = "block";
        document.getElementById("uploadResult").innerHTML = `
          <div class="upload-success">
            ✅ Upload thành công!<br>
            <span class="small-text">
              File: <strong>${data.filename}</strong> | 
              Size: <strong>${data.size}</strong> | 
              MD5: <code>${data.md5}</code> |
              Version: <strong>${data.version}</strong>
            </span>
          </div>
        `;
        // Reset form
        selectedFile = null;
        document.getElementById("uploadForm").style.display = "none";
        document.getElementById("uploadZone").style.display = "flex";
        document.getElementById("firmwareFileInput").value = "";
        document.getElementById("uploadVersion").value = "";
        // Refresh data
        fetchApiData();
      } else {
        alert("Lỗi: " + (data.reason || "Unknown error"));
      }
    } else {
      alert("Upload thất bại! HTTP " + xhr.status);
    }
  });

  xhr.addEventListener("error", function () {
    btnUpload.disabled = false;
    btnUpload.textContent = "⬆️ Upload lên Server";
    alert("Lỗi kết nối khi upload!");
  });

  xhr.open("POST", "/api/upload-firmware");
  xhr.send(formData);
}

// ============================================================
// Version Editor
// ============================================================

function showVersionEditor() {
  const editor = document.getElementById("versionEditor");
  const currentVersion = document.getElementById("serverVersion").textContent;
  document.getElementById("inputNewVersion").value =
    currentVersion !== "-" ? currentVersion : "";
  editor.style.display = "block";
  document.getElementById("inputNewVersion").focus();
}

function hideVersionEditor() {
  document.getElementById("versionEditor").style.display = "none";
}

function submitVersion() {
  const newVersion = document.getElementById("inputNewVersion").value.trim();
  if (!newVersion) {
    alert("Vui lòng nhập version!");
    return;
  }

  fetch("/api/set-version", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ version: newVersion }),
  })
    .then((res) => res.json())
    .then((data) => {
      if (data.ok) {
        hideVersionEditor();
        fetchApiData();
      } else {
        alert("Lỗi: " + (data.reason || "Unknown"));
      }
    })
    .catch((err) => alert("Lỗi kết nối: " + err));
}

// Enter key trong version input
document.addEventListener("DOMContentLoaded", function () {
  const input = document.getElementById("inputNewVersion");
  if (input) {
    input.addEventListener("keydown", function (e) {
      if (e.key === "Enter") submitVersion();
      if (e.key === "Escape") hideVersionEditor();
    });
  }
});

// ============================================================
// Khởi tạo và polling
// ============================================================

fetchApiData();
setInterval(fetchApiData, 1500);
