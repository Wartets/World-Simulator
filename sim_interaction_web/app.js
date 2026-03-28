const TIER_RANK = { A: 1, B: 2, C: 3 };

const appState = {
    selectedTier: "A",
    data: null,
    cy: null,
    selection: null,
    isPhoneLayout: false,
    view: {
        nodeCount: 0,
        edgeCount: 0,
        interactions: [],
    },
};

const graphContainer = document.getElementById("graph");
const tierButtons = Array.from(document.querySelectorAll("[data-tier]"));
const countsLabel = document.getElementById("countsLabel");
const tierDescription = document.getElementById("tierDescription");
const readCount = document.getElementById("readCount");
const writeCount = document.getElementById("writeCount");
const advancedCount = document.getElementById("advancedCount");
const selectionTitle = document.getElementById("selectionTitle");
const selectionSubtitle = document.getElementById("selectionSubtitle");
const detailContent = document.getElementById("detailContent");

tierButtons.forEach((button) => {
    button.addEventListener("click", () => {
        setSelectedTier(button.dataset.tier);
        applyTierGraph(true);
    });
});

function escapeHtml(value) {
    return String(value ?? "")
        .replace(/&/g, "&amp;")
        .replace(/</g, "&lt;")
        .replace(/>/g, "&gt;")
        .replace(/"/g, "&quot;")
        .replace(/'/g, "&#39;");
}

function formatDisplayLabel(value) {
    return String(value ?? "").replaceAll("_", " ");
}

function setSelectedTier(tier) {
    appState.selectedTier = tier;
    tierButtons.forEach((button) => {
        button.setAttribute("aria-pressed", String(button.dataset.tier === tier));
    });

    if (appState.data?.tiers?.[tier]) {
        tierDescription.textContent = appState.data.tiers[tier];
    } else {
        tierDescription.textContent = "";
    }
}

function activeInteractions() {
    if (!appState.data) {
        return [];
    }

    const selectedRank = TIER_RANK[appState.selectedTier];
    return appState.data.interactions.filter((interaction) => TIER_RANK[interaction.minTier] <= selectedRank);
}

function getNodeById(id) {
    return appState.data?.nodes.find((node) => node.id === id) ?? null;
}

function isPhoneLayout() {
    return window.matchMedia("(max-width: 640px)").matches;
}

function makeElements() {
    const interactions = activeInteractions();
    const nodeIds = new Set();

    interactions.forEach((interaction) => {
        nodeIds.add(interaction.source);
        nodeIds.add(interaction.target);
    });

    const nodes = appState.data.nodes
        .filter((node) => nodeIds.has(node.id))
        .map((node) => ({
            data: {
                id: node.id,
                label: formatDisplayLabel(node.label),
                group: node.group,
            },
            classes: node.group,
        }));

    const edges = interactions.map((interaction) => ({
        data: {
            id: interaction.id,
            source: interaction.source,
            target: interaction.target,
            summary: interaction.summary,
            detail: interaction.detail,
            equation: interaction.equation,
            evidence: interaction.evidence,
            minTier: interaction.minTier,
            mode: interaction.mode,
        },
        classes: [interaction.mode, interaction.minTier !== "A" ? "advanced" : ""].filter(Boolean).join(" "),
    }));

    return { nodes, edges, interactions };
}

function buildLayout(animate) {
    const phoneLayout = isPhoneLayout();

    return {
        name: "breadthfirst",
        directed: true,
        circle: false,
        grid: false,
        spacingFactor: phoneLayout ? 1.05 : 1.15,
        avoidOverlap: true,
        nodeDimensionsIncludeLabels: true,
        fit: true,
        padding: phoneLayout ? 24 : 36,
        transform: (_node, position) =>
            phoneLayout
                ? {
                      x: position.x,
                      y: position.y,
                  }
                : {
                      x: position.y,
                      y: position.x,
                  },
        animate,
        animationDuration: 220,
    };
}

function applyResponsiveGraphStyle() {
    if (!appState.cy) {
        return;
    }

    const phoneLayout = isPhoneLayout();
    appState.isPhoneLayout = phoneLayout;

    appState.cy.style()
        .selector("node")
        .style({
            "font-size": phoneLayout ? 30 : 64,
            "text-max-width": phoneLayout ? 190 : 420,
            padding: phoneLayout ? "18px" : "38px",
        })
        .selector("node.subsystem")
        .style({
            padding: phoneLayout ? "22px" : "44px",
        })
        .update();
}

function clearFocus() {
    if (!appState.cy) {
        return;
    }

    appState.cy.batch(() => {
        appState.cy.elements().removeClass("faded active-node active-edge related-node related-edge");
    });
}

function focusNode(node) {
    const cy = appState.cy;
    if (!cy || node.empty()) {
        return;
    }

    const connectedEdges = node.connectedEdges();
    const connectedNodes = connectedEdges.connectedNodes();
    const relatedNodes = connectedNodes.difference(node);
    const neighborhood = node.union(connectedEdges).union(relatedNodes);

    cy.batch(() => {
        cy.elements().addClass("faded");
        neighborhood.removeClass("faded");
        node.addClass("active-node");
        connectedEdges.addClass("related-edge");
        relatedNodes.addClass("related-node");
    });
}

function focusEdge(edge) {
    const cy = appState.cy;
    if (!cy || edge.empty()) {
        return;
    }

    const connectedNodes = edge.connectedNodes();
    const neighborhood = edge.union(connectedNodes);

    cy.batch(() => {
        cy.elements().addClass("faded");
        neighborhood.removeClass("faded");
        edge.addClass("active-edge");
        connectedNodes.addClass("related-node");
    });
}

function updateStats(interactions, nodeCount, edgeCount) {
    const reads = interactions.filter((interaction) => interaction.mode === "read").length;
    const writes = interactions.filter((interaction) => interaction.mode === "write").length;
    const tierAdded = interactions.filter((interaction) => interaction.minTier !== "A").length;

    appState.view = {
        interactions,
        nodeCount,
        edgeCount,
    };

    readCount.textContent = String(reads);
    writeCount.textContent = String(writes);
    advancedCount.textContent = String(tierAdded);
    countsLabel.textContent = `${nodeCount} nodes / ${edgeCount} interactions`;
}

function createSummaryCard(label, value) {
    return `
        <div class="summary-card">
            <span class="label">${escapeHtml(label)}</span>
            <span class="value">${escapeHtml(value)}</span>
        </div>
    `;
}

function createMetaPill(label, value) {
    return `<span class="meta-pill">${escapeHtml(label)} <strong>${escapeHtml(value)}</strong></span>`;
}

function createDetailItem(label, value, options = {}) {
    const className = options.code ? "detail-item code" : "detail-item";
    return `
        <div class="${className}">
            <span class="label">${escapeHtml(label)}</span>
            <span class="value">${escapeHtml(value)}</span>
        </div>
    `;
}

function createInteractionCard(interaction) {
    const source = getNodeById(interaction.source);
    const target = getNodeById(interaction.target);
    const sourceLabel = formatDisplayLabel(source?.label ?? interaction.source);
    const targetLabel = formatDisplayLabel(target?.label ?? interaction.target);

    return `
        <button class="relation-card ${escapeHtml(interaction.mode)}" type="button" data-interaction-id="${escapeHtml(interaction.id)}">
            <div class="relation-top">
                <span class="relation-title">${escapeHtml(interaction.summary)}</span>
                <span class="relation-tag">${escapeHtml(interaction.mode)}</span>
            </div>
            <div class="relation-meta">
                ${escapeHtml(sourceLabel)} -> ${escapeHtml(targetLabel)}<br />
                Minimum tier ${escapeHtml(interaction.minTier)}
            </div>
        </button>
    `;
}

function bindDetailActions() {
    detailContent.querySelectorAll("[data-interaction-id]").forEach((button) => {
        button.addEventListener("click", () => {
            selectInteractionById(button.dataset.interactionId);
        });
    });

    detailContent.querySelectorAll("[data-node-id]").forEach((button) => {
        button.addEventListener("click", () => {
            selectNodeById(button.dataset.nodeId);
        });
    });
}

function renderOverview() {
    const { interactions, nodeCount, edgeCount } = appState.view;
    const reads = interactions.filter((interaction) => interaction.mode === "read").length;
    const writes = interactions.filter((interaction) => interaction.mode === "write").length;

    selectionTitle.textContent = `Tier ${appState.selectedTier} overview`;
    selectionSubtitle.textContent =
        appState.data?.tiers?.[appState.selectedTier] ?? "Choose a node or connection to inspect its details.";

    detailContent.innerHTML = `
        <div class="summary-grid">
            ${createSummaryCard("Visible nodes", nodeCount)}
            ${createSummaryCard("Visible interactions", edgeCount)}
            ${createSummaryCard("Read edges", reads)}
            ${createSummaryCard("Write edges", writes)}
        </div>

        <section class="detail-section">
            <h3>How to use this view</h3>
            <p>
                Click any node in the graph to see the interactions connected to it. Click any connection to open its
                summary, equation, implementation note, and evidence.
            </p>
        </section>
    `;
}

function renderNodeDetails(nodeData) {
    const node = getNodeById(nodeData.id);
    if (!node) {
        renderOverview();
        return;
    }

    const interactions = activeInteractions();
    const incoming = interactions
        .filter((interaction) => interaction.target === node.id)
        .sort((left, right) => left.summary.localeCompare(right.summary));
    const outgoing = interactions
        .filter((interaction) => interaction.source === node.id)
        .sort((left, right) => left.summary.localeCompare(right.summary));

    const typeLabel = node.group === "subsystem" ? "Subsystem" : "State field";
    const incomingTitle = node.group === "subsystem" ? "Incoming dependencies" : "Written by";
    const outgoingTitle = node.group === "subsystem" ? "Outgoing effects" : "Used by";

    selectionTitle.textContent = formatDisplayLabel(node.label);
    selectionSubtitle.textContent = `${typeLabel} visible in tier ${appState.selectedTier}.`;

    detailContent.innerHTML = `
        <div class="meta-pills">
            ${createMetaPill("Type", typeLabel)}
            ${createMetaPill("Incoming", incoming.length)}
            ${createMetaPill("Outgoing", outgoing.length)}
        </div>

        <div class="summary-grid">
            ${createSummaryCard("Linked interactions", incoming.length + outgoing.length)}
            ${createSummaryCard("Active tier", appState.selectedTier)}
        </div>

        <section class="detail-section">
            <h3>${escapeHtml(incomingTitle)}</h3>
            ${
                incoming.length
                    ? `<div class="relation-list">${incoming.map((interaction) => createInteractionCard(interaction)).join("")}</div>`
                    : `<div class="empty-state">No visible incoming interactions for this tier.</div>`
            }
        </section>

        <section class="detail-section">
            <h3>${escapeHtml(outgoingTitle)}</h3>
            ${
                outgoing.length
                    ? `<div class="relation-list">${outgoing.map((interaction) => createInteractionCard(interaction)).join("")}</div>`
                    : `<div class="empty-state">No visible outgoing interactions for this tier.</div>`
            }
        </section>
    `;

    bindDetailActions();
}

function renderInteractionDetails(interaction) {
    const source = getNodeById(interaction.source);
    const target = getNodeById(interaction.target);
    const sourceLabel = formatDisplayLabel(source?.label ?? interaction.source);
    const targetLabel = formatDisplayLabel(target?.label ?? interaction.target);

    selectionTitle.textContent = interaction.summary;
    selectionSubtitle.textContent = `${interaction.mode === "read" ? "Read" : "Write"} interaction in tier ${interaction.minTier}.`;

    detailContent.innerHTML = `
        <div class="meta-pills">
            ${createMetaPill("Mode", interaction.mode)}
            ${createMetaPill("Minimum tier", interaction.minTier)}
            ${createMetaPill("Interaction", interaction.id)}
        </div>

        <div class="detail-list">
            ${createDetailItem("Source", sourceLabel)}
            ${createDetailItem("Target", targetLabel)}
            ${createDetailItem("Detail", interaction.detail)}
            ${createDetailItem("Equation", interaction.equation, { code: true })}
            ${createDetailItem("Evidence", interaction.evidence, { code: true })}
        </div>

        <div class="action-row">
            <button class="secondary-button" type="button" data-node-id="${escapeHtml(interaction.source)}">Inspect source</button>
            <button class="secondary-button" type="button" data-node-id="${escapeHtml(interaction.target)}">Inspect target</button>
        </div>
    `;

    bindDetailActions();
}

function ensureGraph() {
    if (appState.cy) {
        applyResponsiveGraphStyle();
        return;
    }

    appState.cy = window.cytoscape({
        container: graphContainer,
        elements: [],
        style: [
            {
                selector: "node",
                style: {
                    label: "data(label)",
                    "font-size": 64,
                    "font-weight": 700,
                    "text-wrap": "wrap",
                    "text-max-width": 420,
                    color: "#f2f2ff",
                    "text-valign": "center",
                    "text-halign": "center",
                    width: "label",
                    height: "label",
                    shape: "round-rectangle",
                    "background-color": "#1b2230",
                    "border-width": 2,
                    "border-color": "#3c4868",
                    padding: "38px",
                    "transition-property": "opacity, background-color, border-color, width",
                    "transition-duration": "180ms",
                },
            },
            {
                selector: "node.subsystem",
                style: {
                    "background-color": "#243148",
                    "border-color": "#5870a3",
                    padding: "44px",
                },
            },
            {
                selector: "edge",
                style: {
                    width: 3,
                    "curve-style": "bezier",
                    "control-point-step-size": 70,
                    "source-endpoint": "outside-to-node",
                    "target-endpoint": "outside-to-node",
                    "target-arrow-shape": "triangle",
                    "target-arrow-color": "#6d86bb",
                    "line-color": "#6d86bb",
                    "arrow-scale": 1.05,
                    opacity: 0.78,
                    "transition-property": "opacity, width, line-color, target-arrow-color",
                    "transition-duration": "180ms",
                },
            },
            {
                selector: "edge.read",
                style: {
                    "line-color": "#66a8f0",
                    "target-arrow-color": "#66a8f0",
                },
            },
            {
                selector: "edge.write",
                style: {
                    width: 3.4,
                    "line-color": "#82c0ff",
                    "target-arrow-color": "#82c0ff",
                },
            },
            {
                selector: "edge.advanced",
                style: {
                    "line-style": "dashed",
                    "line-dash-pattern": [9, 5],
                },
            },
            {
                selector: ".faded",
                style: {
                    opacity: 0.16,
                },
            },
            {
                selector: "node.active-node",
                style: {
                    "border-width": 4,
                    "border-color": "#4c73b0",
                },
            },
            {
                selector: "node.related-node",
                style: {
                    "border-width": 3,
                    "border-color": "#6d86bb",
                },
            },
            {
                selector: "edge.active-edge",
                style: {
                    width: 5,
                    opacity: 1,
                    "line-color": "#7fb0ff",
                    "target-arrow-color": "#7fb0ff",
                },
            },
            {
                selector: "edge.related-edge",
                style: {
                    width: 4,
                    opacity: 1,
                },
            },
        ],
        wheelSensitivity: 0.2,
    });

    applyResponsiveGraphStyle();

    appState.cy.on("tap", "node", (event) => {
        const node = event.target;
        appState.selection = { type: "node", id: node.id() };
        clearFocus();
        focusNode(node);
        renderNodeDetails(node.data());
    });

    appState.cy.on("tap", "edge", (event) => {
        const edge = event.target;
        appState.selection = { type: "edge", id: edge.id() };
        clearFocus();
        focusEdge(edge);
        renderInteractionDetails(edge.data());
    });

    appState.cy.on("tap", (event) => {
        if (event.target !== appState.cy) {
            return;
        }

        appState.selection = null;
        clearFocus();
        renderOverview();
    });
}

function selectNodeById(id) {
    const node = appState.cy?.getElementById(id);
    if (!node || node.empty() || !node.isNode()) {
        return;
    }

    appState.selection = { type: "node", id };
    clearFocus();
    focusNode(node);
    renderNodeDetails(node.data());
}

function selectInteractionById(id) {
    const edge = appState.cy?.getElementById(id);
    if (!edge || edge.empty() || !edge.isEdge()) {
        return;
    }

    appState.selection = { type: "edge", id };
    clearFocus();
    focusEdge(edge);
    renderInteractionDetails(edge.data());
}

function restoreSelection() {
    if (!appState.selection) {
        return false;
    }

    if (appState.selection.type === "node") {
        const node = appState.cy.getElementById(appState.selection.id);
        if (!node.empty() && node.isNode()) {
            focusNode(node);
            renderNodeDetails(node.data());
            return true;
        }
    }

    if (appState.selection.type === "edge") {
        const edge = appState.cy.getElementById(appState.selection.id);
        if (!edge.empty() && edge.isEdge()) {
            focusEdge(edge);
            renderInteractionDetails(edge.data());
            return true;
        }
    }

    appState.selection = null;
    return false;
}

function applyTierGraph(animate) {
    if (!appState.data) {
        return;
    }

    ensureGraph();

    const { nodes, edges, interactions } = makeElements();
    const cy = appState.cy;

    cy.batch(() => {
        cy.elements().remove();
        cy.add([...nodes, ...edges]);
    });

    updateStats(interactions, nodes.length, edges.length);
    clearFocus();
    renderOverview();

    const layout = cy.layout(buildLayout(animate));
    layout.run();

    if (!restoreSelection()) {
        renderOverview();
    }
}

async function loadInteractionData() {
    const response = await fetch("sim_interaction_web/interaction-data.json", { cache: "no-store" });
    if (!response.ok) {
        throw new Error(`Unable to load interaction data (${response.status})`);
    }

    appState.data = await response.json();
}

async function initialize() {
    try {
        appState.isPhoneLayout = isPhoneLayout();
        setSelectedTier("A");
        await loadInteractionData();
        setSelectedTier("A");
        applyTierGraph(false);

        window.addEventListener("resize", () => {
            const nextPhoneLayout = isPhoneLayout();
            if (nextPhoneLayout === appState.isPhoneLayout) {
                return;
            }

            appState.isPhoneLayout = nextPhoneLayout;
            applyTierGraph(false);
        });
    } catch (error) {
        countsLabel.textContent = `Error: ${error.message}`;
        selectionTitle.textContent = "Unable to load the graph";
        selectionSubtitle.textContent = "The interaction data could not be read.";
        detailContent.innerHTML = `
            <div class="empty-state">${escapeHtml(error.message)}</div>
        `;
    }
}

initialize();
