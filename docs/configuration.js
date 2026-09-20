/**
 * @brief Add a button to open the configuration option for each table
 */
document.addEventListener("DOMContentLoaded", function() {
  const tables = document.querySelectorAll("table");
  tables.forEach(table => {
    if (table.className !== "doxtable") {
      return;
    }

    let previousElement = table.previousElementSibling;
    while (previousElement?.tagName && previousElement.tagName !== "H2") {
      previousElement = previousElement.previousElementSibling;
    }
    if (previousElement?.textContent) {
      const sectionId = previousElement.textContent.trim().toLowerCase();
      const newRow = document.createElement("tr");

      const newCell = document.createElement("td");
      newCell.setAttribute("colspan", "3");

      const openButton = document.createElement("button");
      openButton.className = "open-button";
      openButton.type = "button";
      openButton.textContent = "Open configuration";
      openButton.addEventListener("click", () => {
        const authority = document.getElementById("host-authority").value;
        window.open(`https://${authority}/config/#${sectionId}`, "_blank", "noopener");
      });

      newCell.appendChild(openButton);
      newRow.appendChild(newCell);

      // get the table body
      const tbody = table.querySelector("tbody");

      // Insert at the beginning of the table
      tbody.insertBefore(newRow, tbody.firstChild);
    }
  });
});
