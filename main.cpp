#include <QApplication>
#include <QMainWindow>
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QCheckBox>
#include <QComboBox>
#include <QProgressBar>
#include <QSpinBox>
#include <QMessageBox>

class DemoWindow : public QMainWindow {
    Q_OBJECT

public:
    DemoWindow() {
        setWindowTitle("Qt6 Aesthetics & Features Demo");
        setMinimumSize(400, 300);

        // 1. Central Widget
        // In QMainWindow, you must set a central widget to hold your layout
        QWidget *centralWidget = new QWidget(this);
        setCentralWidget(centralWidget);

        // 2. Main Layout (Vertical)
        QVBoxLayout *mainLayout = new QVBoxLayout(centralWidget);
        mainLayout->setSpacing(20);
        mainLayout->setContentsMargins(20, 20, 20, 20);

        // --- SECTION 1: Form Layout (The "Professional" look) ---
        QLabel *headerLabel = new QLabel("User Profile Settings");
        headerLabel->setStyleSheet("font-size: 18px; font-weight: bold; color: #2c3e50;");
        mainLayout->addWidget(headerLabel);

        QFormLayout *formLayout = new QFormLayout();

        QLineEdit *nameInput = new QLineEdit();
        nameInput->setPlaceholderText("Enter your name...");

        QComboBox *countryCombo = new QComboBox();
        countryCombo->addItems({"USA", "UK", "Germany", "Canada", "Japan"});

        QSpinBox *ageSpin = new QSpinBox();
        ageSpin->setRange(0, 120);
        ageSpin->setValue(25);

        formLayout->addRow("Full Name:", nameInput);
        formLayout->addRow("Country:", countryCombo);
        formLayout->addRow("Age:", ageSpin);

        mainLayout->addLayout(formLayout);

        // --- SECTION 2: Interaction Widgets ---
        QHBoxLayout *optionsLayout = new QHBoxLayout();
        QCheckBox *darkModeCheck = new QCheckBox("Enable Dark Mode (Visual Simulation)");
        optionsLayout->addWidget(darkModeCheck);
        optionsLayout->addStretch(); // Pushes checkbox to the left
        mainLayout->addLayout(optionsLayout);

        // --- SECTION 3: Feedback Widgets ---
        QProgressBar *progressBar = new QProgressBar();
        progressBar->setRange(0, 100);
        progressBar->setValue(0);
        mainLayout->addWidget(new QLabel("System Progress:"));
        mainLayout->addWidget(progressBar);

        // --- SECTION 4: Action Buttons ---
        QHBoxLayout *buttonLayout = new QHBoxLayout();
        QPushButton *btnApply = new QPushButton("Apply Changes");
        QPushButton *btnCancel = new QPushButton("Cancel");

        // Give the Apply button a "Primary" feel via styling
        btnApply->setStyleSheet("background-color: #3498db; color: white; font-weight: bold; padding: 5px;");

        buttonLayout->addWidget(btnCancel);
        buttonLayout->addWidget(btnApply);
        mainLayout->addLayout(buttonLayout);

        // --- LOGIC / SIGNALS & SLOTS ---

        // Connect Apply button to a lambda function
        connect(btnApply, &QPushButton::clicked, [=]() {
            QString name = nameInput->text();
            if (name.isEmpty()) {
                QMessageBox::warning(this, "Error", "Please enter a name first!");
            } else {
                QMessageBox::information(this, "Success",
                    QString("Settings saved for %1 from %2!").arg(name, countryCombo->currentText()));
            }
        });

        // Connect Checkbox to simulate "Dark Mode" by changing the window background
        connect(darkModeCheck, &QCheckBox::toggled, [=](bool checked) {
            if (checked) {
                centralWidget->setStyleSheet("background-color: #2c3e50; color: white;");
                headerLabel->setStyleSheet("font-size: 18px; font-weight: bold; color: #ecf0f1;");
            } else {
                centralWidget->setStyleSheet("");
                headerLabel->setStyleSheet("font-size: 18px; font-weight: bold; color: #2c3e50;");
            }
        });

        // Connect Cancel button to simulate a progress bar loading
        connect(btnCancel, &QPushButton::clicked, [=]() {
            progressBar->setValue(0);
            for(int i = 0; i <= 100; i += 10) {
                progressBar->setValue(i);
                QApplication::processEvents(); // Force UI update during loop
                // Note: In real apps, use QTimer or QThread for this
            }
        });
    }
};

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    DemoWindow window;
    window.show();
    return app.exec();
}

#include "main.moc" // Required if the class is defined in the .cpp file
