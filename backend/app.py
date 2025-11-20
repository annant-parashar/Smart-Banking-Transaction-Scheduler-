from flask import Flask, request, jsonify, send_from_directory
from flask_cors import CORS
import subprocess, json, os
from datetime import datetime

# ---------------------------------------
# PATH SETUP
# ---------------------------------------

BASE_DIR = os.path.dirname(os.path.abspath(__file__))
FRONTEND_DIR = os.path.join(BASE_DIR, "..", "frontend")
DATA_DIR = os.path.join(BASE_DIR, "data")

USERS_FILE = os.path.join(DATA_DIR, "users.json")
ACCOUNTS_FILE = os.path.join(DATA_DIR, "accounts.json")
PENDING_FILE = os.path.join(DATA_DIR, "transactions_pending.json")
PROCESSED_FILE = os.path.join(DATA_DIR, "transactions_processed.json")

AUTO_RESULT_FILE = os.path.join(DATA_DIR, "auto_selected_result.json")

ALL_ALGOS = ["FCFS", "SJF-P", "PRIORITY-P", "RR", "MLQ", "MLFQ"]

# type-based mode
ALGO_MAP = {
    "UPI": "SJF-P",
    "ATM": "RR",
    "NEFT": "FCFS",
    "RTGS": "PRIORITY-P",
    "CHEQUE": "FCFS",
    "EMI": "MLQ"   
}

app = Flask(__name__)
CORS(app)

# JSON HELPERS

def load_json(path, default):
    try:
        with open(path, "r") as f:
            return json.load(f)
    except:
        return default

def save_json(path, data):
    with open(path, "w") as f:
        json.dump(data, f, indent=2)

# BURST TIME RULES


def get_burst(tx_type):
    tx_type = tx_type.upper()
    if tx_type == "UPI":     return 2
    if tx_type == "ATM":     return 2
    if tx_type == "NEFT":    return 3
    if tx_type == "EMI":     return 3
    if tx_type == "CHEQUE":  return 5
    if tx_type == "RTGS":    return 6
    if tx_type == "FRAUD":   return 8
    return 3

# ---------------------------------------
# FRONTEND ROUTES
# ---------------------------------------

@app.route("/")
def home_page():
    return send_from_directory(FRONTEND_DIR, "home.html")

@app.route("/login")
def login_page():
    return send_from_directory(FRONTEND_DIR, "login.html")

@app.route("/customer")
def customer_page():
    return send_from_directory(FRONTEND_DIR, "customer.html")

@app.route("/admin")
def admin_page():
    return send_from_directory(FRONTEND_DIR, "admin.html")

@app.route("/app.js")
def js():
    return send_from_directory(FRONTEND_DIR, "app.js")

@app.route("/style.css")
def css():
    return send_from_directory(FRONTEND_DIR, "style.css")

# LOGIN


@app.route("/api/login", methods=["POST"])
def login():
    data = request.get_json()
    username = data.get("username", "")
    password = data.get("password", "")

    users = load_json(USERS_FILE, [])

    for u in users:
        if u["username"] == username and u["password"] == password:
            resp = {
                "success": True,
                "userId": u["id"],
                "username": u["username"],
                "role": u["role"],
                "status": u.get("status", "ACTIVE")
            }
            if u["role"] == "customer":
                resp["account"] = u["account"]
            return jsonify(resp)

    return jsonify({"success": False, "error": "Invalid credentials"}), 401

# CUSTOMER API

@app.route("/api/customer/summary")
def customer_summary():
    uid = int(request.args.get("userId"))

    users = load_json(USERS_FILE, [])
    accounts = load_json(ACCOUNTS_FILE, [])
    pending = load_json(PENDING_FILE, [])
    processed = load_json(PROCESSED_FILE, [])

    user = next(u for u in users if u["id"] == uid)
    acc = next(a for a in accounts if a["account"] == user["account"])

    my_pending = [t for t in pending if t["customerId"] == uid]
    my_proc = [t for t in processed if t["customerId"] == uid]

    return jsonify({
        "customer": user,
        "balance": acc["balance"],
        "pending": my_pending,
        "processed": my_proc
    })

@app.route("/api/customer/transaction", methods=["POST"])
def create_tx():
    data = request.get_json()

    uid = data["userId"]
    tx_type = data["type"]
    direction = data["direction"]
    amount = float(data["amount"])

    users = load_json(USERS_FILE, [])
    accounts = load_json(ACCOUNTS_FILE, [])
    pending = load_json(PENDING_FILE, [])

    user = next(u for u in users if u["id"] == uid)
    acc = next(a for a in accounts if a["account"] == user["account"])

    # Velocity based fraud
    if "txCount" not in acc:
        acc["txCount"] = 0
    if "status" not in acc:
        acc["status"] = "ACTIVE"

    if acc["status"] == "FROZEN":
        status = "fraud_pending"
    else:
        status = "approved"
        acc["txCount"] += 1
        if acc["txCount"] >= 10:
            acc["status"] = "FROZEN"

    # Funds check (for approved debit)
    if direction == "DEBIT" and status == "approved" and acc["balance"] < amount:
        return jsonify({"success": False, "error": "Insufficient balance"}), 400

    arrival = len(pending) + 1
    next_id = max([t["id"] for t in pending], default=0) + 1
    realtime_now = datetime.now().strftime("%Y-%m-%d %H:%M:%S")

    assigned_algo = ALGO_MAP.get(tx_type.upper(), "FCFS")

    tx = {
        "id": next_id,
        "customerId": uid,
        "account": acc["account"],
        "type": tx_type,
        "direction": direction,
        "amount": amount,
        "arrival": arrival,
        "burst": get_burst(tx_type),
        "status": status,
        "realtime": realtime_now,
        "assignedAlgo": assigned_algo
    }

    pending.append(tx)

    save_json(PENDING_FILE, pending)
    save_json(ACCOUNTS_FILE, accounts)

    return jsonify({"success": True, "status": status})

# ADMIN BASIC


@app.route("/api/admin/pending")
def admin_pending():
    return jsonify(load_json(PENDING_FILE, []))

@app.route("/api/admin/processed")
def admin_processed():
    return jsonify(load_json(PROCESSED_FILE, []))

@app.route("/api/admin/unfreeze", methods=["POST"])
def admin_unfreeze():
    data = request.get_json()
    account = data["account"]

    accounts = load_json(ACCOUNTS_FILE, [])
    pending = load_json(PENDING_FILE, [])

    acc = next((a for a in accounts if a["account"] == account), None)
    if not acc:
        return jsonify({"success": False, "error": "Account not found"}), 400

    # reset fraud state
    acc["status"] = "ACTIVE"
    acc["txCount"] = 0

    for t in pending:
        if t["account"] == account and t["status"] == "fraud_pending":
            t["status"] = "approved"

    save_json(ACCOUNTS_FILE, accounts)
    save_json(PENDING_FILE, pending)

    return jsonify({"success": True})

@app.route("/api/admin/reject", methods=["POST"])
def admin_reject():
    data = request.get_json()
    txid = data["id"]

    pending = load_json(PENDING_FILE, [])
    processed = load_json(PROCESSED_FILE, [])

    new_pending = []
    for t in pending:
        if t["id"] == txid:
            t["status"] = "not_completed"
            t["realCT"] = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
            processed.append(t)
        else:
            new_pending.append(t)

    save_json(PENDING_FILE, new_pending)
    save_json(PROCESSED_FILE, processed)

    return jsonify({"success": True})

# SCHEDULER HELPERS


def run_scheduler_for_algo(approved_list, algo):
    """Writes transactions.json, runs ./scheduler.out <algo>, reads output.json"""
    tx_data = []
    for t in approved_list:
        tx_data.append({
            "id": t["id"],
            "arrival": t["arrival"],
            "burst": t["burst"],
            "type": t["type"]
        })

    with open(os.path.join(BASE_DIR, "transactions.json"), "w") as f:
        json.dump(tx_data, f, indent=2)

    exe = os.path.join(BASE_DIR, "scheduler.out")
    subprocess.run([exe, algo])

    with open(os.path.join(BASE_DIR, "output.json"), "r") as f:
        schedule = json.load(f)

    return schedule

def compute_avg(schedule):
    if not schedule:
        return 0.0, 0.0
    total_wt = sum(p["WT"] for p in schedule)
    total_tat = sum(p["TAT"] for p in schedule)
    n = len(schedule)
    return total_wt / n, total_tat / n

# MANUAL SIMULATION

@app.route("/api/admin/simulate/manual", methods=["POST"])
def simulate_manual():

    data = request.get_json()
    algo = data["algorithm"]

    if algo not in ALL_ALGOS:
        return jsonify({"success": False, "error": "Invalid algorithm"}), 400

    pending = load_json(PENDING_FILE, [])
    accounts = load_json(ACCOUNTS_FILE, [])
    processed = load_json(PROCESSED_FILE, [])

    approved = [t for t in pending if t["status"] == "approved"]

    if not approved:
        return jsonify({"success": False, "error": "No approved transactions"}), 400

    # run scheduler
    schedule = run_scheduler_for_algo(approved, algo)
    # tag algo into schedule entries (for Gantt color / labels)
    for p in schedule:
        p["algorithm"] = algo

    avg_wt, avg_tat = compute_avg(schedule)

    by_id = {p["id"]: p for p in schedule}
    new_pending = []

    for tx in pending:
        if tx["id"] in by_id and tx["status"] == "approved":
            s = by_id[tx["id"]]
            tx["CT"] = s["CT"]
            tx["TAT"] = s["TAT"]
            tx["WT"] = s["WT"]
            tx["status"] = "completed"
            tx["realCT"] = datetime.now().strftime("%Y-%m-%d %H:%M:%S")

            # update balance
            acc = next(a for a in accounts if a["account"] == tx["account"])
            if tx["direction"] == "DEBIT":
                acc["balance"] -= tx["amount"]
            else:
                acc["balance"] += tx["amount"]

            processed.append(tx)
        else:
            new_pending.append(tx)

    save_json(PENDING_FILE, new_pending)
    save_json(PROCESSED_FILE, processed)
    save_json(ACCOUNTS_FILE, accounts)

    return jsonify({
        "success": True,
        "mode": "manual",
        "algorithm": algo,
        "schedule": schedule,
        "avgWT": avg_wt,
        "avgTAT": avg_tat
    })

# ---------------------------------------
# AUTO SIMULATION (single best algo, NO COMMIT)
# ---------------------------------------

@app.route("/api/admin/simulate/auto", methods=["POST"])
def simulate_auto():
    pending = load_json(PENDING_FILE, [])
    approved = [t for t in pending if t["status"] == "approved"]

    if not approved:
        return jsonify({"success": False, "error": "No approved transactions"}), 400

    results = []
    best_algo = None
    best_schedule = None
    best_score = None

    for algo in ALL_ALGOS:
        schedule = run_scheduler_for_algo(approved, algo)
        avg_wt, avg_tat = compute_avg(schedule)
        score = avg_wt + avg_tat

        results.append({
            "algorithm": algo,
            "avgWT": avg_wt,
            "avgTAT": avg_tat
        })

        if best_score is None or score < best_score:
            best_score = score
            best_algo = algo
            best_schedule = schedule

    # tag best schedule with algorithm name
    for p in best_schedule:
        p["algorithm"] = best_algo

    save_json(AUTO_RESULT_FILE, {
        "algorithm": best_algo,
        "schedule": best_schedule
    })

    return jsonify({
        "success": True,
        "mode": "auto",
        "bestAlgorithm": best_algo,
        "bestSchedule": best_schedule,
        "summary": results
    })

# ---------------------------------------
# TYPE-BASED / ASSIGNED SIMULATION + COMMIT
# ---------------------------------------

@app.route("/api/admin/simulate/assigned", methods=["POST"])
def simulate_assigned():
    """
    Run different algorithms for different transaction types
    based on assignedAlgo (or ALGO_MAP), AND COMMIT the result.
    Clears all approved from pending (A option).
    """
    pending = load_json(PENDING_FILE, [])
    accounts = load_json(ACCOUNTS_FILE, [])
    processed = load_json(PROCESSED_FILE, [])

    approved = [t for t in pending if t["status"] == "approved"]

    if not approved:
        return jsonify({"success": False, "error": "No approved transactions"}), 400

    # group by assignedAlgo / mapping
    groups = {}
    for t in approved:
        alg = t.get("assignedAlgo") or ALGO_MAP.get(t["type"].upper(), "FCFS")
        groups.setdefault(alg, []).append(t)

    combined = []
    stats = []

    for algo, txs in groups.items():
        schedule = run_scheduler_for_algo(txs, algo)
        # annotate with algorithm for display
        for p in schedule:
            p["algorithm"] = algo

        avg_wt, avg_tat = compute_avg(schedule)
        stats.append({
            "algorithm": algo,
            "count": len(schedule),
            "avgWT": avg_wt,
            "avgTAT": avg_tat
        })

        combined.extend(schedule)

    # sort for nice display
    combined.sort(key=lambda p: (p.get("CT", 0), p.get("id", 0)))
    avg_wt_all, avg_tat_all = compute_avg(combined)

    # COMMIT: apply combined schedule to pending
    by_id = {p["id"]: p for p in combined}
    new_pending = []

    for tx in pending:
        if tx["id"] in by_id and tx["status"] == "approved":
            s = by_id[tx["id"]]
            tx["CT"] = s["CT"]
            tx["TAT"] = s["TAT"]
            tx["WT"] = s["WT"]
            tx["status"] = "completed"
            tx["realCT"] = datetime.now().strftime("%Y-%m-%d %H:%M:%S")

            acc = next(a for a in accounts if a["account"] == tx["account"])
            if tx["direction"] == "DEBIT":
                acc["balance"] -= tx["amount"]
            else:
                acc["balance"] += tx["amount"]

            processed.append(tx)
        else:
            new_pending.append(tx)

    save_json(PENDING_FILE, new_pending)
    save_json(PROCESSED_FILE, processed)
    save_json(ACCOUNTS_FILE, accounts)

    return jsonify({
        "success": True,
        "mode": "assigned",
        "combinedSchedule": combined,
        "stats": stats,
        "avgWT": avg_wt_all,
        "avgTAT": avg_tat_all
    })

# ---------------------------------------
# COMMIT AUTO (APPLY BEST ALGO)
# ---------------------------------------

@app.route("/api/admin/commit/auto", methods=["POST"])
def commit_auto():
    if not os.path.exists(AUTO_RESULT_FILE):
        return jsonify({"success": False, "error": "Run auto simulation first"}), 400

    auto_data = load_json(AUTO_RESULT_FILE, {})
    schedule = auto_data.get("schedule", [])
    best_algo = auto_data.get("algorithm", "UNKNOWN")

    if not schedule:
        return jsonify({"success": False, "error": "Invalid auto file"}), 400

    pending = load_json(PENDING_FILE, [])
    accounts = load_json(ACCOUNTS_FILE, [])
    processed = load_json(PROCESSED_FILE, [])

    by_id = {p["id"]: p for p in schedule}
    new_pending = []
    committed = []

    for tx in pending:
        if tx["id"] in by_id and tx["status"] == "approved":
            s = by_id[tx["id"]]
            tx["CT"] = s["CT"]
            tx["TAT"] = s["TAT"]
            tx["WT"] = s["WT"]
            tx["status"] = "completed"
            tx["realCT"] = datetime.now().strftime("%Y-%m-%d %H:%M:%S")

            acc = next(a for a in accounts if a["account"] == tx["account"])
            if tx["direction"] == "DEBIT":
                acc["balance"] -= tx["amount"]
            else:
                acc["balance"] += tx["amount"]

            committed.append(tx)
            processed.append(tx)
        else:
            new_pending.append(tx)

    save_json(PENDING_FILE, new_pending)
    save_json(PROCESSED_FILE, processed)
    save_json(ACCOUNTS_FILE, accounts)

    try:
        os.remove(AUTO_RESULT_FILE)
    except:
        pass

    avg_wt, avg_tat = compute_avg(schedule)

    return jsonify({
        "success": True,
        "algorithmUsed": best_algo,
        "processedCount": len(committed),
        "schedule": schedule,
        "avgWT": avg_wt,
        "avgTAT": avg_tat
    })

# ---------------------------------------
# START SERVER
# ---------------------------------------

if __name__ == "__main__":
    app.run(debug=True)
