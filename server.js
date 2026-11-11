
import express from "express";
import fs from "fs";
import path from "path";
import { fileURLToPath } from "url";

const app = express();
const PORT = 5050;

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);

const DATA_FILE = path.join(__dirname, "medications.json");
const HISTORY_FILE = path.join(__dirname, "dose-history.json");
const PET_FILE = path.join(__dirname, "pet.json");

app.use(express.json());

// Create JSON files if they do not exist
if (!fs.existsSync(DATA_FILE)) {
    fs.writeFileSync(DATA_FILE, "[]", "utf8");
}

if (!fs.existsSync(HISTORY_FILE)) {
    fs.writeFileSync(HISTORY_FILE, "[]", "utf8");
}

if (!fs.existsSync(PET_FILE)) {
    fs.writeFileSync(
        PET_FILE,
        JSON.stringify({
            petId: "tv",
            growthPoints: 0
        }, null, 2),
        "utf8"
    );
}

// Read JSON file
function readJSON(file) {
    return JSON.parse(fs.readFileSync(file, "utf8"));
}

// Save JSON file
function saveJSON(file, data) {
    fs.writeFileSync(
        file,
        JSON.stringify(data, null, 2),
        "utf8"
    );
}

// Get current pet growth stage
function getPetStage(growthPoints) {
    if (growthPoints >= 10) return "adult";
    if (growthPoints >= 5) return "teen";
    return "baby";
}

// Read pet information
function readPet() {
    return readJSON(PET_FILE);
}

// Read medications
function readMedications() {
    return readJSON(DATA_FILE);
}

// Save medications
function saveMedications(medications) {
    saveJSON(DATA_FILE, medications);
}

// Check backend connection
app.get("/api/health", (req, res) => {
    res.json({ status: "ok" });
});

// GET pet information
app.get("/api/pet", (req, res) => {
    try {
        const pet = readPet();

        res.json({
            petId: pet.petId,
            growthPoints: pet.growthPoints,
            growthStage: getPetStage(pet.growthPoints)
        });

    } catch (error) {
        res.status(500).json({
            error: "Failed to load pet information"
        });
    }
});

// Update selected pet
app.put("/api/pet", (req, res) => {
    try {
        const allowedPets = ["tv", "cloud", "ghost"];
        const selectedPet = req.body.petId;

        if (!allowedPets.includes(selectedPet)) {
            return res.status(400).json({
                error: "Invalid pet selection"
            });
        }

        const pet = readPet();

        // Only change the selected pet.
        // Growth points and awarded doses remain untouched.
        pet.petId = selectedPet;

        saveJSON(PET_FILE, pet);

        res.json({
            success: true,
            petId: pet.petId,
            growthPoints: pet.growthPoints,
            growthStage: getPetStage(pet.growthPoints)
        });

    } catch (error) {
        console.error("Pet update error:", error);

        res.status(500).json({
            error: "Failed to update pet"
        });
    }
});

// GET all medications
app.get("/api/medications", (req, res) => {
    try {
        res.json(readMedications());
    } catch (error) {
        res.status(500).json({
            error: "Failed to load medications"
        });
    }
});

// POST all medications
app.post("/api/medications", (req, res) => {
    try {
        const medications = req.body;

        if (!Array.isArray(medications)) {
            return res.status(400).json({
                error: "Medications must be an array"
            });
        }

        if (medications.length > 10) {
            return res.status(400).json({
                error: "Maximum 10 reminders allowed"
            });
        }

        saveMedications(medications);

        res.json({
            success: true,
            count: medications.length
        });
    } catch (error) {
        res.status(500).json({
            error: "Failed to save medications"
        });
    }
});

// PUT: Add or update one medication
app.put("/api/medications/:id", (req, res) => {
    try {
        const medications = readMedications();
        const medication = req.body;
        const id = req.params.id;

        if (
            !medication ||
            typeof medication !== "object" ||
            Array.isArray(medication) ||
            String(medication.id) !== id ||
            typeof medication.name !== "string" ||
            !medication.name.trim() ||
            medication.name.length > 20 ||
            typeof medication.time !== "string" ||
            !/^([01]\d|2[0-3]):[0-5]\d$/.test(medication.time) ||
            !Array.isArray(medication.days) ||
            medication.days.length === 0 ||
            !medication.days.every(day =>
                Number.isInteger(day) && day >= 0 && day <= 6
            ) ||
            typeof medication.message !== "string" ||
            medication.message.length > 16
        ) {
            return res.status(400).json({
                error: "Invalid medication data"
            });
        }

        const index = medications.findIndex(
            med => String(med.id) === id
        );

        if (index >= 0) {
            medications[index] = medication;
        } else {
            if (medications.length >= 10) {
                return res.status(400).json({
                    error: "Maximum 10 reminders allowed"
                });
            }

            medications.push(medication);
        }

        saveMedications(medications);

        res.json({
            success: true,
            medication: medication
        });
    } catch (error) {
        res.status(500).json({
            error: "Failed to save medication"
        });
    }
});

// DELETE one medication
app.delete("/api/medications/:id", (req, res) => {
    try {
        const medications = readMedications();

        const updated = medications.filter(
            med => String(med.id) !== req.params.id
        );

        saveMedications(updated);

        res.json({
            success: true,
            message: "Medication deleted"
        });
    } catch (error) {
        res.status(500).json({
            error: "Failed to delete medication"
        });
    }
});

// GET medication history
app.get("/api/dose-history", (req, res) => {
    try {
        res.json(readJSON(HISTORY_FILE));
    } catch (error) {
        res.status(500).json({
            error: "Failed to load history"
        });
    }
});

// POST medication history
app.post("/api/dose-history", (req, res) => {
    try {
        const history = readJSON(HISTORY_FILE);
        const entry = req.body;

        if (
            !entry ||
            typeof entry !== "object" ||
            Array.isArray(entry) ||
            !entry.id ||
            !entry.medicationId ||
            !entry.timestamp
        ) {
            return res.status(400).json({
                error: "Invalid history entry"
            });
        }

        // Check whether this acknowledgement already exists
        const existing = history.find(
            item => String(item.id) === String(entry.id)
        );

        if (!existing) {
            // Only award growth for actual medication acknowledgements
            if (entry.status === "taken") {
                const pet = readPet();

                // Initialise tracking for existing pet.json files
                if (!Array.isArray(pet.awardedDoseIds)) {
                    pet.awardedDoseIds = [];
                }

                if (!pet.awardedDoseIds.includes(String(entry.id))) {
                    pet.growthPoints = Math.min(
                        10,
                        Math.max(0, Number(pet.growthPoints) || 0) + 1
                    );

                    pet.awardedDoseIds.push(String(entry.id));

                    // Save pet information first
                    saveJSON(PET_FILE, pet);
                }
            }

            // Save medication history
            history.push(entry);
            saveJSON(HISTORY_FILE, history);
        }

        const pet = readPet();

        res.json({
            success: true,
            pet: {
                petId: pet.petId,
                growthPoints: pet.growthPoints,
                growthStage: getPetStage(pet.growthPoints)
            }
        });

    } catch (error) {
        console.error("Dose history error:", error);

        res.status(500).json({
            error: "Failed to save history"
        });
    }
});

// TinyCircuits downloads medication reminders
app.get("/api/device/medications", (req, res) => {
    try {
        res.json(readMedications());
    } catch (error) {
        res.status(500).json({
            error: "Failed to retrieve medications"
        });
    }
});

// Serve the website
app.get("/", (req, res) => {
    res.sendFile(path.join(__dirname, "indexSE.html"));
});

// Serve CSS and other website files
app.use(express.static(__dirname));

// Start server
app.listen(PORT, "0.0.0.0", () => {
    console.log("--------------------------------");
    console.log("Medication Reminder Server");
    console.log(`Website: http://localhost:${PORT}`);
    console.log(`API: http://localhost:${PORT}/api/medications`);
    console.log("--------------------------------");
});