#include <Arduino.h>

#ifndef Y_COUNTER_UI_H
#define Y_COUNTER_UI_H

#endif

const char mainPage[] PROGMEM = R"=====(
<!DOCTYPE html>
<html lang="en">

<style>
    html, body {
        height: 100%;
    }

    body {
        display: flex;
        background-color: #ECEFF1;
    }

    .level-counter {
        font-family: Courier, monospace;
        letter-spacing: 2px;
        font-size: 350%;
        text-align: justify;
        display: flex;
        justify-content: center;
        padding-bottom: 5%;
    }

    .container {
        width: 90%;
        margin: auto;
    }

    .radiation-level-chart {
        display: flex;
    }

</style>

<head>
    <meta charset="UTF-8">
    <title>γ-meter</title>
    <link rel="icon" type="image/png" sizes="16x16"
          href="data:image/png;base64,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"/>
</head>
<body>

<div class="container">
    <script src="https://cdnjs.cloudflare.com/ajax/libs/Chart.js/2.5.0/Chart.min.js"></script>

    <div id="current-radiation-level" class="level-counter"></div>

    <div class="radiation-level-chart">
        <canvas id="radiation-level-chart"></canvas>
    </div>
</div>

<script>
    const counter_data_name = "y_meter_data"
    const counter_labels_name = "y_meter_labels"
    const counter_colors_name = "y_meter_colors"

    function fetchArray(name) {
        if (localStorage.getItem(name) == null) {
            localStorage.setItem(name, '[]')
        }

        console.log(JSON.parse(localStorage.getItem(name)))

        return JSON.parse(localStorage.getItem(name))
    }

    function updateArray(name, value) {
        localStorage.setItem(name, JSON.stringify(value))
    }

    function updateCurrentMeasurements() {
        let values = fetchArray(counter_data_name)
        value = values[values.length - 1]

        if (value !== undefined) {
            document.getElementById("current-radiation-level").textContent = value.toFixed(1) + " μR/HR"
        }
    }

    function getColor(value) {

        if (value < 20) {
            return "#66BB6A";
        }
        if (value > 50) {
            return "#EF5350";
        }

        return "#FFA726";
    }

    function formatTimeUnit(unit) {
        if (unit < 10) {
            return "0" + unit
        }

        return unit
    }

    function updateColors() {
        const colors = []
        for (i = 0; i < data.length; i++) {
            colors[i] = getColor(data[i]);
        }

        return colors
    }

    let data = fetchArray(counter_data_name)
    let labels = fetchArray(counter_labels_name)
    let colors = fetchArray(counter_colors_name)

    updateCurrentMeasurements()

    const chart = new Chart("radiation-level-chart", {
        type: 'bar',
        data: {
            labels: labels,
            datasets: [{
                data: data,
                backgroundColor: updateColors(),
            }
            ]
        },
        options: {
            elements: {
                point: {
                    radius: 0
                }
            },
            animation: false,
            responsive: true,
            legend: {
                display: false
            },
            tooltips: {
                enabled: true
            },
            scales: {
                xAxes: [{
                    display: true
                }],
                yAxes: [{
                    display: true,
                    ticks: {
                        beginAtZero: true,
                        steps: 10,
                        stepValue: 0.1
                    }
                }]
            }
        }
    });


    setInterval(function () {
        fetch('statistic')
            .then((response) => {
                return response.json()
            })
            .then((data) => {
                return data["staticIntensity"];
            })
            .then((value) => {
                let today = new Date();
                let date = formatTimeUnit(today.getHours()) + ":" + formatTimeUnit(today.getMinutes()) + ":" + formatTimeUnit(today.getSeconds())

                if (labels.length > 60) {
                    labels.shift()
                    data.shift()
                    colors.shift()
                }

                data.push(value)
                labels.push(date)
                colors.push(getColor(value))

                updateArray(counter_data_name, data)
                updateArray(counter_labels_name, labels)
                updateArray(counter_colors_name, colors)
                chart.data.datasets[0].backgroundColor = colors

                updateCurrentMeasurements()
                chart.update()
            });
    }, 1000);

</script>

</body>
</html>
)=====";