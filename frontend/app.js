// ======================================================
// LOGIN / LOGOUT
// ======================================================

function loginUser() {
    let username = document.getElementById("loginUser").value;
    let password = document.getElementById("loginPass").value;

    fetch("/api/login", {
        method: "POST",
        headers: {"Content-Type": "application/json"},
        body: JSON.stringify({username, password})
    })
    .then(res => res.json())
    .then(data => {
        if (!data.success) {
            alert("Invalid username or password");
            return;
        }
        localStorage.setItem("user", JSON.stringify(data));
        window.location = data.role === "admin" ? "/admin" : "/customer";
    });
}

function logout() {
    localStorage.removeItem("user");
    window.location = "/login";
}

function requireLogin(role) {
    let u = localStorage.getItem("user");
    if (!u) {
        window.location = "/login";
        return null;
    }
    u = JSON.parse(u);
    if (role && u.role !== role) {
        alert("Access denied");
        window.location = "/login";
        return null;
    }
    return u;
}

// ======================================================
// CUSTOMER DASHBOARD
// ======================================================

function loadCustomerDashboard() {
    let u = requireLogin("customer");
    if (!u) return;

    fetch(`/api/customer/summary?userId=${u.userId}`)
        .then(res => res.json())
        .then(data => {
            document.getElementById("customerInfo").innerHTML =
                `<b>Name:</b> ${data.customer.username}<br>
                 <b>Balance:</b> ₹${data.balance}`;

            // Pending
            let p = "";
            data.pending.forEach(t => {
                p += `<tr>
                    <td>${t.id}</td>
                    <td>${t.type}</td>
                    <td>${t.amount}</td>
                    <td>${t.realtime}</td>
                    <td>${t.status}</td>
                </tr>`;
            });
            document.querySelector("#pendingTable tbody").innerHTML = p;

            // Completed
            let r = "";
            data.processed.forEach(t => {
                let status = t.status === "completed" ? "Completed" : "Not Completed";
                r += `<tr>
                    <td>${t.id}</td>
                    <td>${t.type}</td>
                    <td>${t.amount}</td>
                    <td>${t.realCT || "-"}</td>
                    <td>${status}</td>
                </tr>`;
            });
            document.querySelector("#processedTable tbody").innerHTML = r;
        });
}

function createTransaction() {
    let u = JSON.parse(localStorage.getItem("user"));

    let type = document.getElementById("txType").value;
    let direction = document.getElementById("txDirection").value;
    let amount = document.getElementById("txAmount").value;

    fetch("/api/customer/transaction", {
        method: "POST",
        headers: {"Content-Type":"application/json"},
        body: JSON.stringify({
            userId: u.userId,
            type, direction, amount
        })
    })
    .then(res => res.json())
    .then(data => {
        if (!data.success) {
            alert(data.error || "Failed");
            return;
        }
        loadCustomerDashboard();
    });
}

// ======================================================
// ADMIN DASHBOARD
// ======================================================

function loadAdminDashboard() {
    let u = requireLogin("admin");
    if (!u) return;

    fetch("/api/admin/pending")
        .then(res => res.json())
        .then(data => {
            let rows = "";
            data.forEach(t => {
                let color = t.status === "fraud_pending" ? "red" : "black";
                rows += `<tr>
                    <td>${t.id}</td>
                    <td>${t.customerId}</td>
                    <td>${t.account}</td>
                    <td>${t.type}</td>
                    <td>${t.amount}</td>
                    <td>${t.realtime}</td>
                    <td>${t.burst}</td>
                    <td style="color:${color}">${t.status}</td>
                    <td><button onclick="unfreeze('${t.account}')">Unfreeze</button></td>
                    <td><button onclick="rejectTx(${t.id})">Reject</button></td>
                </tr>`;
            });

            document.querySelector("#adminPending tbody").innerHTML = rows;
        });
}

function unfreeze(account) {
    fetch("/api/admin/unfreeze", {
        method: "POST",
        headers: {"Content-Type":"application/json"},
        body: JSON.stringify({account})
    })
    .then(res => res.json())
    .then(_ => loadAdminDashboard());
}

function rejectTx(id) {
    fetch("/api/admin/reject", {
        method: "POST",
        headers: {"Content-Type": "application/json"},
        body: JSON.stringify({id})
    })
    .then(res => res.json())
    .then(_ => loadAdminDashboard());
}

// ======================================================
// MANUAL SIMULATION + COMMIT
// ======================================================

function simulateManual() {
    let algo = document.getElementById("algoSelect").value;

    fetch("/api/admin/simulate/manual", {
        method: "POST",
        headers: {"Content-Type":"application/json"},
        body: JSON.stringify({algorithm: algo})
    })
    .then(res => res.json())
    .then(data => {
        if (!data.success) {
            alert(data.error || "Manual simulation failed");
            return;
        }

        document.getElementById("simulationInfo").innerText =
            `Manual | ${data.algorithm} | WT=${data.avgWT.toFixed(2)} TAT=${data.avgTAT.toFixed(2)}`;

        renderScheduleTable(data.schedule);
        renderGantt(data.schedule);
        loadAdminDashboard();   // pending cleared & refreshed
    });
}

// ======================================================
// AUTO SIMULATION (NO COMMIT)
// ======================================================

function simulateAuto() {
    fetch("/api/admin/simulate/auto", {
        method: "POST",
        headers: {"Content-Type":"application/json"}
    })
    .then(res => res.json())
    .then(data => {
        if (!data.success) {
            alert(data.error || "Auto simulation failed");
            return;
        }

        document.getElementById("autoInfo").innerHTML =
            `<b>Best Algorithm:</b> ${data.bestAlgorithm}`;

        // Summary table
        let sum = "";
        data.summary.forEach(row => {
            sum += `<tr>
                <td>${row.algorithm}</td>
                <td>${row.avgWT.toFixed(2)}</td>
                <td>${row.avgTAT.toFixed(2)}</td>
            </tr>`;
        });
        document.querySelector("#autoSummary tbody").innerHTML = sum;

        // Auto result table using bestSchedule
        let r = "";
        data.bestSchedule.forEach(p => {
            r += `<tr>
                <td>P${p.id}</td>
                <td>${p.type}</td>
                <td>${p.arrival}</td>
                <td>${p.burst}</td>
                <td>${p.CT}</td>
                <td>${p.TAT}</td>
                <td>${p.WT}</td>
                <td>${p.algorithm || data.bestAlgorithm}</td>
            </tr>`;
        });
        document.querySelector("#autoResultTable tbody").innerHTML = r;

        // Auto Gantt chart
        renderAutoGantt(data.bestSchedule);
    });
}

// ======================================================
// TYPE-BASED SIMULATION + COMMIT
// ======================================================

function simulateAssigned() {
    fetch("/api/admin/simulate/assigned", {
        method: "POST",
        headers: {"Content-Type": "application/json"}
    })
    .then(res => res.json())
    .then(data => {
        if (!data.success) {
            alert(data.error || "Type-based simulation failed");
            return;
        }

        document.getElementById("simulationInfo").innerText =
            `Type-Based Simulation | Avg WT=${data.avgWT.toFixed(2)} TAT=${data.avgTAT.toFixed(2)}`;

        renderScheduleTable(data.combinedSchedule);
        renderGantt(data.combinedSchedule);
        loadAdminDashboard();   // pending cleared & refreshed
    });
}

// ======================================================
// COMMIT AUTO (APPLY BEST ALGO)
// ======================================================

function commitAuto() {
    fetch("/api/admin/commit/auto", {
        method: "POST",
        headers: {"Content-Type": "application/json"}
    })
    .then(res => res.json())
    .then(data => {
        if (!data.success) {
            alert(data.error || "Commit failed");
            return;
        }

        alert(
            `Committed using ${data.algorithmUsed}\n` +
            `Processed: ${data.processedCount}\n` +
            `Avg WT: ${data.avgWT.toFixed(2)}, Avg TAT: ${data.avgTAT.toFixed(2)}`
        );

        // show final schedule from auto commit
        renderScheduleTable(data.schedule);
        renderGantt(data.schedule);
        renderAutoGantt(data.schedule);

        loadAdminDashboard();
    });
}

// ======================================================
// TABLE RENDER
// ======================================================

function renderScheduleTable(list) {
    let rows = "";
    list.forEach(p => {
        rows += `<tr>
            <td>P${p.id}</td>
            <td>${p.type}</td>
            <td>${p.arrival}</td>
            <td>${p.burst}</td>
            <td>${p.CT}</td>
            <td>${p.TAT}</td>
            <td>${p.WT}</td>
        </tr>`;
    });
    document.querySelector("#resultTable tbody").innerHTML = rows;
}

// ======================================================
// GANTT CHART (MANUAL / TYPE-BASED)
// ======================================================

function renderGantt(list) {
    const container = document.getElementById("ganttContainer");
    if (!container) return;
    container.innerHTML = "";

    if (!list || !list.length) return;

    let maxCT = Math.max(...list.map(p => p.CT));
    let scale = 800 / maxCT;

    list.forEach(p => {
        const row = document.createElement("div");
        row.className = "gantt-row";

        const label = document.createElement("span");
        label.className = "gantt-label";
        label.innerText = `P${p.id}`;

        const timeline = document.createElement("div");
        timeline.className = "gantt-timeline";

        const spacer = document.createElement("div");
        spacer.className = "gantt-spacer";
        spacer.style.width = (p.arrival * scale) + "px";

        const bar = document.createElement("div");
        bar.className = "gantt-bar";
        bar.style.width = ((p.CT - p.arrival) * scale) + "px";
        bar.title = `Start=${p.arrival} End=${p.CT}`;

        // stylish algo label
        const algoLabel = document.createElement("span");
        algoLabel.className = "algo-label";
        algoLabel.innerText = p.algorithm || p.assignedAlgo || "";
        bar.appendChild(algoLabel);

        timeline.appendChild(spacer);
        timeline.appendChild(bar);

        row.appendChild(label);
        row.appendChild(timeline);
        container.appendChild(row);
    });
}

// ======================================================
// AUTO GANTT CHART
// ======================================================

function renderAutoGantt(list) {
    const container = document.getElementById("autoGantt");
    if (!container) return;
    container.innerHTML = "";

    if (!list || !list.length) return;

    let maxCT = Math.max(...list.map(p => p.CT));
    let scale = 800 / maxCT;

    list.forEach(p => {
        const row = document.createElement("div");
        row.className = "gantt-row";

        const label = document.createElement("span");
        label.className = "gantt-label";
        label.innerText = `P${p.id}`;

        const timeline = document.createElement("div");
        timeline.className = "gantt-timeline";

        const spacer = document.createElement("div");
        spacer.className = "gantt-spacer";
        spacer.style.width = (p.arrival * scale) + "px";

        const bar = document.createElement("div");
        bar.className = "gantt-bar";
        bar.style.width = ((p.CT - p.arrival) * scale) + "px";
        bar.title = `Start=${p.arrival} End=${p.CT}`;

        const algoLabel = document.createElement("span");
        algoLabel.className = "algo-label";
        algoLabel.innerText = p.algorithm || "";
        bar.appendChild(algoLabel);

        timeline.appendChild(spacer);
        timeline.appendChild(bar);

        row.appendChild(label);
        row.appendChild(timeline);
        container.appendChild(row);
    });
}
