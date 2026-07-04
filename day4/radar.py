import serial
import pygame
import math
import sys

# --- CONFIGURATION ---
# Change 'COM4' to your Arduino's port if needed (e.g., '/dev/ttyUSB0' on Mac/Linux)
SERIAL_PORT = 'COM4'
BAUD_RATE = 9600

# Screen and Radar Settings
WIDTH, HEIGHT = 1200, 1000
CENTER_X, CENTER_Y = WIDTH // 2, HEIGHT // 2
MAX_DISTANCE = 200  # Maximum cm to draw
SCALE = 3.5  # Multiplier to make the map fit the screen nicely
RING_DISTANCES = [50, 100, 150, 200]

# Colors
BLACK = (0, 0, 0)
GREEN = (0, 255, 0)
DARK_GREEN = (0, 70, 0)

# --- INITIALIZATION ---
pygame.init()
screen = pygame.display.set_mode((WIDTH, HEIGHT))
pygame.display.set_caption("Ultrasonic 2D Radar Map")
clock = pygame.time.Clock()
font = pygame.font.SysFont("consolas", 14)

# Try to connect to the Arduino
try:
    arduino = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=0.05)
    print(f"Connected to Arduino on {SERIAL_PORT}")
except Exception as e:
    print(f"Error connecting to {SERIAL_PORT}: {e}")
    print("Did you close the Arduino IDE Serial Monitor?")
    sys.exit()

# List to store our mapped points with distance values.
points = []  # Each entry: (x, y, distance)
current_angle = 0
previous_angle = 0


def draw_radar_background():
    """Draws the retro green radar arcs and labels."""
    screen.fill(BLACK)

    # Draw distance rings
    for dist in RING_DISTANCES:
        radius = int(dist * SCALE)
        pygame.draw.circle(screen, DARK_GREEN, (CENTER_X, CENTER_Y), radius, 2)

        label = font.render(f"{dist}cm", True, DARK_GREEN)
        screen.blit(label, (CENTER_X + radius + 6, CENTER_Y - 8))

    # Draw angle lines
    for angle in range(0, 360, 30):
        rad = math.radians(angle)
        x = CENTER_X + int(MAX_DISTANCE * SCALE * math.cos(rad))
        y = CENTER_Y - int(MAX_DISTANCE * SCALE * math.sin(rad))
        pygame.draw.line(screen, DARK_GREEN, (CENTER_X, CENTER_Y), (x, y), 2)


# --- MAIN LOOP ---
running = True
while running:
    for event in pygame.event.get():
        if event.type == pygame.QUIT:
            running = False

    # 1. Read data from Arduino
    if arduino.in_waiting > 0:
        try:
            # Read line, decode bytes to string, and remove extra whitespace/newlines
            line = arduino.readline().decode('utf-8').strip()

            if "," in line:
                angle_str, dist_str = line.split(",")
                angle = int(angle_str)
                distance = int(dist_str)

                # Detect direction change (when angle drops significantly, we've completed a rotation)
                if previous_angle > 180 and angle < 180:
                    # Direction change detected - clear all points for fresh sweep
                    points = []

                previous_angle = current_angle
                current_angle = angle % 360

                # Ignore out-of-bounds glitch readings
                if distance < MAX_DISTANCE:
                    # Math: Convert Polar (angle, distance) to Cartesian (x, y)
                    rad = math.radians(angle)
                    x = CENTER_X + int(distance * math.cos(rad) * SCALE)
                    y = CENTER_Y - int(distance * math.sin(rad) * SCALE)

                    # Store point with its distance value
                    point = (x, y, distance)
                    points.append(point)

        except ValueError:
            # Sometimes the serial line gets garbled halfway through, just ignore it
            pass

    # 2. Draw Visuals
    draw_radar_background()

    # Draw the sweeping radar line
    rad = math.radians(current_angle)
    sweep_x = CENTER_X + int(MAX_DISTANCE * SCALE * math.cos(rad))
    sweep_y = CENTER_Y - int(MAX_DISTANCE * SCALE * math.sin(rad))
    pygame.draw.line(screen, GREEN, (CENTER_X, CENTER_Y), (sweep_x, sweep_y), 3)

    # Draw the obstacle points without labels or connecting lines
    for point in points:
        x, y, distance = point
        pygame.draw.circle(screen, GREEN, (x, y), 5)

    pygame.display.flip()
    clock.tick(60)  # Run at 60 Frames Per Second

# Clean up on exit
arduino.close()
pygame.quit()