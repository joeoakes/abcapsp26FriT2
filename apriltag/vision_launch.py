import os
from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch_ros.actions import Node

def generate_launch_description():
    # Find the default 36h11 tag configuration file automatically
    apriltag_config = os.path.join(
        get_package_share_directory('apriltag_ros'),
        'cfg',
        'tags_36h11.yaml'
    )

    # 1. The Camera Node
    camera_node = Node(
        package='camera_ros',
        executable='camera_node',
        name='camera_node',
        output='screen',
        parameters=[{
            'width': 640,
            'height': 480,
            'format': 'BGR888'
        }],
        remappings=[
            ('/camera/image_raw', '/image_raw'),
            ('/camera/camera_info', '/camera_info')
        ]
    )

    # 2. The AprilTag Detector Node
    apriltag_node = Node(
        package='apriltag_ros',
        executable='apriltag_node',
        name='apriltag_node',
        output='screen',
        parameters=[
            apriltag_config,
            {'pose_estimation_method': ''} # Disables the calibration crash
        ],
        remappings=[
            ('/image_rect', '/image_raw'),
            ('/camera_info', '/camera_info')
        ]
    )

    # Launch them both!
    return LaunchDescription([
        camera_node,
        apriltag_node
    ])
