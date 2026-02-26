function renderFiles(files) {
  const fwTable = document
    .getElementById("firmwareTable")
    .querySelector("tbody");
  fwTable.innerHTML = "";

  if (!files || files.length === 0) {
    fwTable.innerHTML =
      '<tr><td colspan="3" class="empty-state">Không tìm thấy file firmware nào.</td></tr>';
    return;
  }

  files.forEach((f) => {
    const row = document.createElement("tr");
    row.innerHTML = `
            <td><a href="/${f.name}" target="_blank">📄 ${f.name}</a></td>
            <td class="file-size">${f.size}</td>
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

function renderDevices(devices, versionClients, activeDownloads) {
  const tableBody = document
    .getElementById("pendingTable")
    .querySelector("tbody");
  tableBody.innerHTML = "";

  let hasAlert = false;

  // Create a unified list of devices by IP
  // Some devices might only have checked the version, others might have requested OTA
  const allDevicesMap = {};

  // First, process devices that have requested OTA
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

  // Next, merge in devices that only checked version
  if (versionClients) {
    Object.entries(versionClients).forEach(([ip, info]) => {
      if (allDevicesMap[ip]) {
        allDevicesMap[ip].version_checks = info.count;
        // If the version check is newer than the OTA request, update timestamp?
        // Let's keep the OTA timestamp if it exists, as it's more relevant.
      } else {
        allDevicesMap[ip] = {
          mac: "-",
          ip: ip,
          chip: "-",
          cores: "?",
          app_name: "-",
          app_version: "-",
          flash_kb: 0,
          ota_status: "none", // Just checked version
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

  // Sort by timestamp or status
  deviceAnysize.sort((a, b) => {
    if (a.ota_status === "pending" && b.ota_status !== "pending") return -1;
    if (a.ota_status !== "pending" && b.ota_status === "pending") return 1;
    return 0;
  });

  deviceAnysize.forEach((dev) => {
    const isDownloading = activeDownloads && activeDownloads[dev.ip];
    if (dev.ota_status === "pending") hasAlert = true;

    const row = document.createElement("tr");

    // Progress UI
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

    // Status Badge
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

    // Action Buttons
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

// Initial fetch and polling every 1.5 seconds for snappier progress bar
fetchApiData();
setInterval(fetchApiData, 1500);
